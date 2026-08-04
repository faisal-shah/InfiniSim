# InfiniSim Multi-Companion Plan

## Goal

Exercise real portable InfiniTime BLE policy through deterministic simulator
ports without claiming RF, SMP cryptography, controller timing, or electrical
power fidelity.

## Architecture

- Compile radio, bond, codec, persistence, access, and management policy from
  the selected `InfiniTime_DIR`.
- Keep virtual peers, GAP outcomes, time, storage faults, and power cuts in
  `sim/ble/`.
- Enforce generated characteristic access policy in the TCP GATT bridge.
- Allow one active bridge client; reject a second as busy.
- Bind the test-control endpoint to loopback only.

## Completed

1. Portable firmware policy integration.
2. Deterministic peer/radio/persistence adapters and control protocol.
3. Authentication gating, management service, one-client ownership.
4. Native tests, headless GATT smoke, CI, and fidelity documentation.

## Remaining

Use physical watches for RF visibility, SMP, resolving-list, CCCD, and power
acceptance through `pinetime-dev-tools/RELEASE.md`.
