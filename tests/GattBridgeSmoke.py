#!/usr/bin/env python3

import os
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path


def free_port() -> int:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def receive_exact(stream: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = stream.recv(size - len(data))
        if not chunk:
            raise RuntimeError("short GATT bridge response")
        data.extend(chunk)
    return bytes(data)


def request(stream: socket.socket, characteristic: int, operation: int, payload: bytes = b"") -> tuple[int, bytes]:
    stream.sendall(bytes((characteristic, operation)) + struct.pack("<H", len(payload)) + payload)
    header = receive_exact(stream, 3)
    return header[0], receive_exact(stream, struct.unpack("<H", header[1:])[0])


def wait_for_control(port: int, process: subprocess.Popen[bytes]) -> None:
    for _ in range(200):
        if process.poll() is not None:
            raise RuntimeError(f"simulator exited with {process.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.1) as stream:
                stream.sendall(b"QUERY\n")
                if receive_exact(stream, 3) == b"OK ":
                    return
        except OSError:
            time.sleep(0.025)
    raise RuntimeError("virtual BLE control endpoint did not start")


def main() -> int:
    simulator = Path(sys.argv[1]).resolve()
    gatt_port = free_port()
    control_port = free_port()
    while control_port == gatt_port:
        control_port = free_port()

    for path in (Path("infinisim-ble-bonds.bin"), Path("infinisim-ble-bonds.bin.next")):
        path.unlink(missing_ok=True)

    environment = dict(os.environ, SDL_VIDEODRIVER="dummy")
    process = subprocess.Popen(
        [
            str(simulator),
            "--hide-status",
            "--gatt-bridge",
            str(gatt_port),
            "--ble-control",
            str(control_port),
        ],
        env=environment,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        wait_for_control(control_port, process)
        vectors = (
            (17, b"\x01"),
            (18, b"Focused Artist"),
            (19, b"Focused Track"),
            (20, b"Focused Album"),
            (21, bytes.fromhex("0000002a")),
            (22, bytes.fromhex("0000012c")),
            (23, bytes.fromhex("00000007")),
            (24, bytes.fromhex("0000000c")),
            (25, bytes.fromhex("0000007d")),
            (26, b"\x01"),
            (27, b"\x01"),
        )
        with socket.create_connection(("127.0.0.1", gatt_port), timeout=3) as stream:
            for characteristic, payload in vectors:
                assert request(stream, characteristic, 0, payload) == (0, b"")
                assert request(stream, characteristic, 1) == (0, b"")
            assert request(stream, 28, 1) == (0xFE, b"")
    finally:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
        for path in (Path("infinisim-ble-bonds.bin"), Path("infinisim-ble-bonds.bin.next")):
            path.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
