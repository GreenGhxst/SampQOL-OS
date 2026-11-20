---
name: mpc-plugin-test
description: Test an MPC plugin on the computer with the SDK's host simulator before putting it on hardware: load sequence, audio rendering to WAV, MIDI, state round trip, parameter sweeps, stability and CPU. Use after every build, when the user reports no sound, noise, crashes, glitches or lost settings, or asks to verify a plugin.
---

> **Installed in SampQOL-OS from the MPC Plugin Builder SDK.** The SDK is a git submodule at `mpc-plugin-builder-sdk/` (run `git submodule update --init` once). Every path below (`tools/`, `docs/`, `framework/`, `plugins/`, `ui-kit/`, `schemas/`, `examples/`) is relative to that folder, so `cd mpc-plugin-builder-sdk` before running the commands, and read `mpc-plugin-builder-sdk/AGENTS.md` first.

# Test with the host simulator

The simulator imitates the MPC's host (same load sequence, 44.1 kHz, small blocks, concurrent-looking parameter writes, the parameter-0 nudge). It cannot replace a test on the real MPC, but it catches most crashes and DSP bugs.

```
.sdk-tools/host_sim plugins/<Name>/build/native/<slug>.dylib dump      # what the MPC will see: name, uid, params, kinds
.sdk-tools/host_sim plugins/<Name>/build/native/<slug>.dylib test --seconds 3 --wav plugins/<Name>/build/test.wav
   # instruments: --notes 48,52,55 ; --playing simulates transport ; effects get a sine + noise burst
   # --block 32 / --block 512 for extreme block sizes ; --verbose logs every opcode
```
(On Linux the native library is `.so`, on Windows `.dll` and the simulator is `host_sim.exe`.)

Read the report:
- `FAIL` lines must be fixed: NaN/Inf output, crash (exit 2, with the phase named), state not round-tripping, parameter get/set mismatch.
- `WARN`: silence from an instrument (no voice triggered?), clipping (reduce gain), denormals, high CPU, effect not passing silence.
- Timing: the simulator reports CPU on your machine; the MPC is 10–40× slower. Keep the simulator's average under a few percent.

Then listen to the WAV (ask the user to, or describe what to expect). For "no sound" problems: check `isNoteOn` handling, envelope attack/gate, output gain, and that `ctx.outputs` is written. For "clicks": smooth parameters, add short attack/release, avoid resetting phase on every block.

When the tests pass, ASK whether they want it installed on a standalone MPC over the network for a real test (never install without a yes; ask for the IP address). See `mpc-plugin-deploy`.

Finally remind the user to test on the MPC itself (`docs/install-and-test.md`): copy the folder into `VST2 Plugins`, enable Preferences → Enhancements (Beta) → External Plugin Support, load it, save and reload a project.
