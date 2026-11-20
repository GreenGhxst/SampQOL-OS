# Tests (specification section 14)

Run with `./build.sh` (desktop) and `./build.sh arm` (aarch64 under QEMU).
Logs: `logs/ctest-desktop-x86_64.txt`, `logs/ctest-qemu-aarch64.txt`,
`logs/check-counts-desktop.txt`, `logs/verify-arm64.txt`.

| # | Required test | Where | Result (desktop) | Result (QEMU aarch64) |
| --- | --- | --- | --- | --- |
| 1 | Valid plugin discovery | `test_plugin_manager` (scan: mock instrument and effect found `ok`, kinds and params right, nested folder, approved folders only, symlinks skipped) | pass | pass |
| 2 | Invalid ELF rejection | `test_plugin_manager` (text file, 2-byte file, ELF executable not ET_DYN, library without entry point) | pass | pass |
| 3 | Wrong architecture rejection | `test_plugin_manager` (other 64-bit machine; 32-bit ARM with the bridge note) | pass | pass |
| 4 | Missing dependency handling | `test_plugin_manager` (`bad_missing_dep.so` links a deleted library: rejected by name before any load) | pass | pass |
| 5 | VST2 entry-point validation | `test_host_core` (no entry, wrong magic, huge I/O, no processReplacing, nonexistent file) | pass | pass |
| 6 | Plugin lifecycle | `test_host_core` (load, activate, deactivate, re-activate at another rate/block, destroy) | pass | pass |
| 7 | MIDI Note On/Off processing | `test_host_core` (silence before, sound after, decay after off; identical output at block 64 and 128) | pass | pass |
| 8 | Audio buffer bounds | `test_host_core` (no write past `frames`, over-size block refused, late events dropped, null channel, event overflow) | pass | pass |
| 9 | Parameter updates | `test_host_core` (names, labels, display, clamping, out-of-range, programs) | pass | pass |
| 10 | Preset serialization | `test_host_core` (chunk and parameter round trips, CRC damage, truncation, identity mismatch, file round trip, orphan state) | pass | pass |
| 11 | Bypass and unload | `test_host_core` (effect bypass, instrument bypass, whole-chain bypass, clear/dlclose, render after) | pass | pass |
| 12 | Crashing plugin behavior | `test_plugin_manager` (crash in open, crash in 3rd process call, hang: each quarantined with signal/timeout; scanner survives; quarantine persists, clears, re-counts) | pass | pass |
| 13 | Unsupported SampQOL version | `test_compat` (unknown build id, no build id, known build but unverified, SampQOL not loaded: all disabled with reason) | pass | pass |
| 14 | Recovery from failed startup | `test_compat` (crash guard: early stop disables until reboot, 3-start limit, fresh boot) | pass | pass |
| 15 | Failure-free ordinary SampQOL startup when the host is disabled | `test_compat` (settings off and DISABLE file win before any other check; `compat_table()` has no verified entry) + by construction: nothing in the release is started at boot | pass (logic) | pass (logic) |

Extra: `test_engine` (MIDI queue order/timing/overflow, transport, perf
monitor, MIDI script parser, WAV round trips); `cli_render`, `cli_info`,
`cli_compat_not_in_app` (end to end through the executable).

Counts (desktop): `test_host_core` 120 checks, `test_plugin_manager` 85,
`test_engine` 65, `test_compat` 30, all passing.

## What these tests do not show

- Anything about the MPC Sample's hardware, kernel, ALSA setup or CPU budget.
- Anything about SampQOL: test 15 proves the gate's logic, not a boot of the
  device. The on-device evidence for "SampQOL starts normally with the
  expansion present" is still to be collected (the expansion adds no boot
  files, so the expected result is trivially the stock behaviour).
- QEMU user emulation runs the aarch64 instructions but with the PC's
  kernel and timing; it does not stand in for a device test.
