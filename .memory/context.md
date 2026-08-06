# Project Context

## Overview

This `family-features` simulator compiles the family InfiniTime checkout rather
than carrying a second BLE policy. Implementation commit `824d512` contains the
multi-companion simulation work.

## Architecture

- `infinitime-ble-portable` compiles policy directly from `InfiniTime_DIR`.
- `sim/ble/VirtualBleAdapter.*` owns deterministic peer, radio, and store ports.
- `VirtualBleControlServer` exposes loopback-only test control.
- `sim/gatt_bridge.*` routes generated bridge IDs to real firmware services and
  enforces one active client plus injected authentication state.

## Tech Stack

C++20, CMake, SDL2, LVGL, littlefs, Python smoke tests, and the family
InfiniTime source tree.

## Invariants

- `InfiniTime_DIR` must contain the family protocol and portable BLE sources.
- Generated metadata digest must match `protocol/companion.json`.
- A second GATT client receives busy and never replaces the incumbent.
- The control endpoint binds only to `127.0.0.1`.
- Virtual security is injected state, never an SMP success claim.
- Flash/wake-lock counters are software proxies only.
- First-format persistence keeps virtual advertising off until the asynchronous
  write succeeds, matching 2.0.2 hardware behavior.

## Key Decisions

| Decision | Rationale | Date |
|---|---|---|
| Compile firmware policy directly | One source of truth for behavior | 2026-08-04 |
| Keep deterministic adapters simulator-only | Useful coverage without false RF fidelity | 2026-08-04 |
| Preserve named atomic power-cut boundaries | Test complete old/new snapshots, never impossible torn live files | 2026-08-04 |
