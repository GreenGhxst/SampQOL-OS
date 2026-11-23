# DISKMODE P0.4 — Durable recovery control-plane architecture

**Status: implemented and simulation-tested only. NOT a production authorization to control the MPC display.**

## Architecture

- `src/native/recovery_control.{h,cpp}`: a POSIX owner-only (0700) state directory with `openat(..., O_NOFOLLOW)`, strict owner/mode/regular-file checks, nonblocking `flock` singleton lock, and append-only fsync'd `recovery.journal`. The directory itself is fsync'd on initial file creation. Record format `DMR2|sequence|phase|public-id|checksum` includes an integrity checksum for accidental corruption and strictly consecutive sequence numbers. **FNV checksum is not an HMAC or proof of provenance.** The journal size is capped at 1 MiB; corruption or torn final records fail closed.
- A 256-bit random secret from `/dev/urandom` accompanies an independent random lease ID. Only the ID is journaled. Secrets are held in RAM, use a full-byte comparison, and are rejected on revocation, restart and mismatched credentials. **This is local in-process capability checking, not yet a production authenticated IPC protocol.**
- State transitions: `idle → prepared → active → revoked → recovered`. A crashed/restarted supervisor sees `prepared`, `active`, `revoked` or `failed` and durably records `recovery_pending`. No new lease is issued until a higher-level restoration component reports success; failure stays closed. A direct `active → recovered` transition is forbidden.
- `tests/core/test_recovery_control.cpp`: credentials, replay, corrupted/torn journal, unauthorized directory permissions, process lock contention, restart reconciliation and restoration failures.
- `src/native/recovery_process_sim.cpp` (single host-only `diskmode-recovery-sim` executable): `supervisor:kill`, `supervisor:exit`, `supervisor:restore-fail`, and `supervisor:second-instance` exercise supervisor crashes, restart reconciliation and lock contention. A surviving harness reopens the persistent journal, proves old credentials are invalid, and records **simulated** stock restoration. This does not control the stock service.
- Existing `recovery_process_sim.cpp`: child worker crash, revocation, timeouts and restoration fault injection continue unchanged.

## Boundaries

- `RecoveryControl` does **not** implement `RecoveryGuard` or `OwnershipAuthority`, cannot call `drmSetMaster`, cannot stop/start systemd, and has **no production IPC endpoint**. The only provided production-safe guards are `NoRecoveryGuard` and `RefuseOwnership`, which always deny access. Real display takeover remains disabled.
- `flock` prevents two cooperative supervisors on the same host/filesystem only. It is not distributed locking or protection against the same UID/root manipulating records. The journal is not a cryptographically signed or remotely attested ledger.
- A successful disk journal write and process isolation do not prove atomic stock-app handoff, actual CRTC restoration or watchdog recovery if the supervisor and recovery monitor both die, the system loses power, or a real stock restart fails.
- The harness calls `_exit` / `SIGKILL` on its **own forked simulation child**. It never sends a signal to stock/SampQOL or opens any DRM/ALSA device.
- The host simulation uses temporary files under `/tmp`, cleaned after tests. A future device service requires a carefully selected non-removable, crash-consistent state directory, independent secure boot/startup and protection against SD export/removal.

## Production blockers

1. An independently installed, device-verified recovery monitor supervising both the stock service and DISKMODE, including supervisor PID liveness, heartbeat, systemd restart semantics and takeover restoration after power loss.
2. Authenticated IPC with peer-credential checks (`SO_PEERCRED`), nonce/replay handling, bounded expiry, runtime lease ownership and process-exit revocation.
3. A trusted, device-specific persistent directory and permission model, crash/power-cut fsync verification, journal rotation, independent restoration evidence, and denial on unreconciled state.
4. Physical MPC Sample exclusive-handoff tests, DRM orientation and CRTC restore, ALSA ownership, audio continuity, and `SIGKILL`/power-loss testing with a known-good fallback.
5. No changes to SampQOL signed autorun; no real DRM/KMS or systemd operations without independent approval and evidence.

## Verification

```sh
cmake -S webconsole -B webconsole/build -DCMAKE_BUILD_TYPE=Release
cmake --build webconsole/build -j2
ctest --test-dir webconsole/build --output-on-failure
webconsole/build/diskmode-supervisor-failure-sim kill
```

Tests validate **host simulation and software invariants** only. AArch64 cross-compilation does not verify hardware runtime.
