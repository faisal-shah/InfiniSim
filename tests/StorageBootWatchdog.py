#!/usr/bin/env python3

import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def main() -> int:
    binary = Path(sys.argv[1]).resolve()
    bridge_port = free_port()
    control_port = free_port()
    while control_port == bridge_port:
        control_port = free_port()

    with tempfile.TemporaryDirectory(prefix="infinisim-storage-boot-") as run:
        env = dict(os.environ)
        env["SDL_VIDEODRIVER"] = "dummy"
        env["INFINISIM_STORAGE_BOOT_DELAY_MS"] = "6000"
        process = subprocess.Popen(
            [
                str(binary),
                "--hide-status",
                "--gatt-bridge",
                str(bridge_port),
                "--ble-control",
                str(control_port),
            ],
            cwd=run,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            start_new_session=True,
        )
        try:
            deadline = time.monotonic() + 12
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    break
                try:
                    with socket.create_connection(
                        ("127.0.0.1", bridge_port), timeout=0.1
                    ):
                        break
                except OSError:
                    time.sleep(0.05)
            time.sleep(8)
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
            output, _ = process.communicate(timeout=5)

    if "[watchdog] STARVED" in output:
        print(output)
        return 1
    if "displayapp task started!" not in output:
        print(output)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
