# DISKMODE P0 independent recovery supervisor — SIMULATION ONLY

Date: 2026-11-22. Branch: `codex/diskmode-integrated-core`.

## What exists

- `src/native/recovery_sim.{h,cpp}`: deterministic, injectable `Supervisor` and `MockStock` state machines; recovery state never grants a real `RecoveryGuard` or ALSA/DRM ownership.
- `src/native/recovery_process_sim.cpp`: a **separate parent process** manages a forked fake DISKMODE child, checks child readiness, monitors termination with `waitpid`, escalates to `SIGKILL` if it does not exit, and restores only a simulated in-memory stock-service state.
- `tests/core/test_recovery_sim.cpp`: revoked lease, abnormal exit, failed restoration, failed stop, unknown stock state, and one-time restore.
- CTest process scenarios: `normal`, `sigkill`, `child-crash` (SIGABRT, core dumps disabled), `revoke` (SIGTERM), `timeout` (bounded escalation), `restore-fail`, `stop-fail`.
- The simulator binary is **host-only**, not built in cross-compilation. No `systemctl`, `drmSetMaster`, ALSA input, physical service manipulation, signed autorun or firmware operations are performed.

## Safety / limitations

This verifies only **process separation and simulated restoration** when the parent supervisor remains alive. It does **not** survive the parent crashing, host power failure, systemd failure, loss of persistent recovery state, unavailable stock panel drivers, invalid CRTC IDs after process death, concurrent launches, or a failed/partially completed real stock-service restart.

The only production-safe authority remains `NoRecoveryGuard`, and `RefuseOwnership` denies hardware access. A passing test of this simulator **must not** cause a production `RecoveryGuard` to be activated.

## Production exit criteria

1. Obtain a verified stock/SampQOL exclusive-handoff contract and reversible launch chain **without editing its signed autorun files**.
2. Introduce an independent privileged recovery service with systemd supervision, ownership/lease authentication, exclusive locks, stale-pid/replay prevention, and persistent crash journal on suitable storage.
3. Add bounded watchdog/heartbeat, startup-order and kill/reboot recovery (including when supervisor dies), plus resumption of the correct prior stock service.
4. Prove stock panel-driver and display restoration on an actual MPC Sample with a sacrificial SD, disconnected network and a documented, tested recovery path. Confirm kernel/DRM capabilities before any takeover.
5. Only then consider a separately reviewed, device-gated production `RecoveryGuard`. Physical gates A–E remain uncleared.

## Host check

```sh
cmake -S webconsole -B webconsole/build -DCMAKE_BUILD_TYPE=Release
cmake --build webconsole/build -j2
ctest --test-dir webconsole/build --output-on-failure
webconsole/build/diskmode-recovery-sim sigkill
```

There is no production start or install command in this simulator.
