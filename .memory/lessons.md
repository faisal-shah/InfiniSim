# Lessons Learned

## Gotchas

- Direct GATT callback invocation can bypass lifecycle and security policy.
- A new TCP socket must not silently replace an existing watch connection.
- Fresh, missing, invalid, and reset stores have distinct expected behavior.
- Two concurrent BlueZ/RF devices cannot be proven by this simulator.

## Patterns

- Compile portable policy from `InfiniTime_DIR`.
- Generate IDs and access flags from the shared manifest.
- Use virtual time and named faults instead of wall-clock sleeps.
- State fidelity limits beside every simulated assertion.

## Decisions

| Decision | Rationale | Date |
|---|---|---|
| Default bridge peer is authenticated | Preserve existing companion simulator compatibility | 2026-08-04 |
| Test control is opt-in and loopback-only | Avoid exposing a remote control surface | 2026-08-04 |

## Checkpoint Log

| Date | Tasks Since Last Checkpoint | Notes |
|---|---:|---|
| 2026-08-04 | 6 | BLE policy integration, tests, CI, docs, and commit complete |
