# Status matrix (specification section 16)

Updated 2026-11-20. Build: `mpc-vst-host` 0.2.0-dev. Logs in `docs/logs/`.

Legend: **yes** = done and shown by a log here; **no** = not done; **n/a**.

| Deliverable | Implemented | Compiled | Desktop-tested (x86_64) | ARM64-built | ARM64 run under QEMU | MPC Sample hardware-tested | SampQOL-integrated |
| --- | --- | --- | --- | --- | --- | --- | --- |
| VST2 ABI header (independent) | yes | yes | yes | yes | yes | no | n/a |
| Dynamic library loader + entry-point discovery | yes | yes | yes | yes | yes | no | no |
| AEffect validation and resource limits | yes | yes | yes | yes | yes | no | no |
| Host callback (time, rate, block, canDo, strings) | yes | yes | yes | yes | yes | no | no |
| Plug-in lifecycle (open/activate/process/deactivate/close/dlclose) | yes | yes | yes | yes | yes | no | no |
| Parameter manager (get/set/name/label/display/automatable) | yes | yes | yes | yes | yes | no | no |
| Programs (get/set/name) | yes | yes | yes | yes | yes | no | no |
| Chunk state (get/set) | yes | yes | yes | yes | yes | no | no |
| Sidecar state file (identity, pad/track, program, body, CRC) | yes | yes | yes | yes | yes | no | no |
| Non-finite output guard | yes | yes | yes | yes | yes | no | no |
| ELF inspector (class, machine, type, DT_NEEDED, RUNPATH, dynsym entry) | yes | yes | yes | yes | yes | no | n/a |
| Dependency inspector | yes | yes | yes | yes | yes | no | n/a |
| Architecture validator | yes | yes | yes | yes | yes | no | n/a |
| Scan worker (fork/exec, RLIMIT_AS/CPU, timeout, signal report) | yes | yes | yes | yes | yes (with launcher prefix) | no | n/a |
| Registry cache + quarantine (plain text) | yes | yes | yes | yes | yes | no | n/a |
| Timestamped SPSC MIDI queue, deterministic order | yes | yes | yes | yes | yes | no | no |
| Transport and tempo info | yes | yes | yes | yes | yes | no | no |
| Performance monitor (per-block, budget, overloads) | yes | yes | yes | yes | yes | no | no |
| Plug-in chain (instrument -> effects, bypass, emergency bypass, all notes off) | yes | yes | yes | yes | yes | no | no |
| Offline renderer + MIDI script + WAV I/O | yes | yes | yes | yes | yes | no | n/a |
| Command line host `mpcvst` | yes | yes | yes | yes | yes | no | n/a |
| Mock instrument (sample-accurate, 4 params, 4 programs, chunk) | yes | yes | yes | yes | yes | no | n/a |
| Mock effect (gain + low-pass) | yes | yes | yes | yes | yes | no | n/a |
| Faulty plug-ins (9 kinds) | yes | yes | yes | yes | yes | no | n/a |
| SampQOL compat gate (env detection, table, fail-closed, crash guard) | yes | yes | yes | yes | yes | no | **no: no verified hook** |
| SampQOL runtime adapter (pad MIDI in, audio out through the sampler) | **no** | no | no | no | no | no | no |
| SD launcher (autorun chain, setarch stand-in) | **no** (documented) | no | no | no | no | no | no |
| Native 320x240 UI (v0.4) | no | no | no | no | no | no | no |
| ARM32 bridge (v0.6) | no (design noted) | no | no | no | no | no | no |
| FM-1 instrument (v0.5) | no (blocked on the host milestones) | no | no | no | no | no | no |
| SD card package (`VST-Expansion/`) | yes | yes | n/a | yes | n/a | no | no |

## Milestone exit conditions

- **v0.1** "A compatible VST2 test instrument generates correct offline audio
  through the host": **met on desktop and under QEMU** (`test_host_core`
  test 7: silence before note on, sound after, decay after note off, same
  samples at block sizes 64 and 128; `cli_render` writes a WAV through
  instrument + effect).
- **v0.2** "The host passes repeatable audio/MIDI and state-restoration
  tests": **met on desktop and under QEMU** (`test_engine`, `test_host_core`
  tests 9 and 10).
- **v0.3** "A test instrument plays from physical pads through the actual
  SampQOL audio path": **not met**; stopped at the working standalone host
  as the specification instructs, blocker documented in
  `SAMPQOL_INTEGRATION.md`.

## Measurements

All numbers are offline renders of the mock instrument (8 voices, one
chord of 6 notes every 0.5 s) at 44.1 kHz, 128-frame blocks (budget
2902.5 µs). They describe the machines they ran on, **not the MPC Sample**.

- Desktop x86_64 (`logs/bench-desktop-x86_64.txt`): the empty host costs
  well under 1 µs per block; one instrument instance a few µs (about 0.1 %
  of the budget), eight instances about 1 %. Eight effects in series about
  0.3 %.
- aarch64 under QEMU user emulation on the same PC
  (`logs/bench-qemu-aarch64.txt`): emulation is roughly 20 times slower
  than native here and says nothing about a Cortex-A55; it only shows the
  code runs. Use `package/run-selftest.sh` on the device for real numbers.

No voice limit or safe plug-in count is claimed.

## Known limitations of this release

- In-process plug-ins are experimental: a plug-in crash in `mpcvst render`
  ends that process. Only the scanner isolates plug-ins.
- The scanner's `missing_dependencies()` searches fixed system folders and
  the plug-in's run path; a device with libraries elsewhere needs
  `--lib DIR`.
- SysEx and MIDI output from plug-ins are not routed.
- `mpcvst` runs at any rate, but a plug-in that assumes 44.1 kHz is not
  resampled (Melodee's Open MPC rate bridge has no counterpart here yet).
- Nothing in this release is started by the MPC at boot.
