# Progress

> **RULE: After each completed task or gate, update this file before moving
> on. Durable state lives here, not in chat history.**

## Resume Here

- Next task: P2-T4
- Next action: validate default configuration with CMake 3.28.3, then support
  the physical fleet gate from `../pinetime-dev-tools/RELEASE.md`.
- Last checkpoint: 2026-08-04 23:44 UTC

## Phase 1 - Policy integration

- [x] P1-T1 compile portable InfiniTime BLE policy (2026-08-04)
- [x] P1-T2 add virtual radio, peer, and persistence adapters (2026-08-04)
- [x] P1-T3 add loopback control and one-client ownership (2026-08-04)
- [x] GATE-P1 - adapter tests pass (2026-08-04)

## Phase 2 - Integration validation

- [x] P2-T1 enforce generated access/authentication metadata (2026-08-04)
- [x] P2-T2 add GATT smoke and family-tree CI checkout (2026-08-04)
- [x] P2-T3 pass all eight ptlab headless scenarios (2026-08-04)
- [ ] P2-T4 replace removed FindPythonInterp with FindPython3
- [ ] GATE-P2 - clean CMake 3.28 configure/build/tests pass

## Phase 3 - Physical ship gate

- [ ] P3-T1 consume real-watch RF/SMP/CCCD findings without weakening fidelity
- [ ] GATE-P3 - keep simulator claims aligned with measured hardware behavior

## Blocked

- Physical behavior requires deployed watches and independent centrals.
