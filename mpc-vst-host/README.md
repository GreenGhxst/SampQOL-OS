# MPC Sample VST Host Expansion (`mpc-vst-host`)

A VST2 plug-in host for the Akai MPC Sample, built to run from the microSD
card beside [SampQOL OS](https://github.com/snarebes/SampQOL-OS) without
replacing it, flashing anything or shipping anyone else's files.

**Status: v0.2 standalone host.** The host loads, validates, scans and
renders aarch64 Linux VST2 plug-ins and passes its tests on a PC and, cross-
built, under QEMU. It has **not** run on an MPC Sample yet and it does
**not** play plug-ins from the pads inside SampQOL: no verified way to route
plug-in audio and pad MIDI through the stock sampler exists, so the SampQOL
adapter is present only as a fail-closed gate. `docs/STATUS.md` says exactly
what is implemented, compiled, desktop-tested, ARM64-built, hardware-tested
and SampQOL-integrated; `docs/SAMPQOL_INTEGRATION.md` explains the blocker.

## What is here

```
include/mpcvst/          public headers
  vst2_abi.h             the VST 2.4 binary interface, written independently (no Steinberg SDK)
  vst2_plugin.h          one loaded plug-in: load, open, activate, process, params, programs, chunks
  elf_inspect.h          ELF class/machine/type, DT_NEEDED, run path, exported entry point: no execution
  plugin_scanner.h       approved-folder scan, static validation, isolated worker probe
  plugin_registry.h      the cached registry and quarantine (plain text on the card)
  midi_queue.h           lock-free timestamped MIDI ring, deterministic ordering
  transport.h            tempo, play state, ppq, bar start
  perf_monitor.h         per-block timing against the block budget
  plugin_chain.h         the audio engine adapter: instrument -> effects, bypass, all-notes-off
  offline_render.h       the standalone test adapter: MIDI script -> WAV
  state_file.h           sidecar state ("MVXS"): identity, pad/track, program, chunk or params, CRC
  sampqol_compat.h       the SampQOL gate: environment detection, compatibility table, crash guard
src/                     implementations, by module
src/tools/mpcvst_cli.cpp     mpcvst: scan | info | render | bench | compat
src/tools/mpcvst_probe.cpp   mpcvst-probe: the scanner's worker process
plugins/                 mock instrument, mock effect, nine faulty plug-ins for the negative tests
tests/                   the automated tests (specification section 14, tests 1-15)
cmake/aarch64-linux-gnu.cmake  cross toolchain for the device
package/                 SD card folder builder, static checks, on-device self-test
docs/                    AUDIT, LICENSES, SAMPQOL_INTEGRATION, STATUS, TESTS, INSTALL, RECOVERY, logs/
launcher/                reserved (see its README)
```

## Build and test

```sh
./build.sh          # desktop: build/ + ctest
./build.sh arm      # device: build-arm64/ + static checks + ctest under QEMU when installed
./build.sh package  # device build, then dist/VST-Expansion/ and zips
```

Needs CMake 3.16+, GCC or Clang with C++17; for the device build
`gcc-aarch64-linux-gnu` and `g++-aarch64-linux-gnu`; optionally
`qemu-user-static`.

## Try it

```sh
B=build
$B/mpcvst info   $B/plugins/mock_instrument.so
$B/mpcvst scan   --worker $B/mpcvst-probe --cache /tmp/registry.txt $B/plugins
$B/mpcvst render $B/plugins/mock_instrument.so --effect $B/plugins/mock_effect.so \
                 --midi tests/data/chord.midi.txt --seconds 2 --out chord.wav --save-state chord.mvxs
$B/mpcvst bench  $B/plugins/mock_instrument.so --instances 4
$B/mpcvst compat # the SampQOL gate: "disabled" everywhere but inside the stock app with a verified build
```

## Design in one paragraph

A plug-in file is first described without running it (`inspect_elf`:
machine, type, needed libraries, exported entry point). Plausible files are
opened by a separate worker process under memory, CPU and time limits; a
crash or hang there quarantines the plug-in and never reaches the host. A
validated plug-in is loaded in-process with `RTLD_LOCAL`, validated again
(magic, channel counts, parameter counts, latency, `processReplacing`), and
run by a `PluginChain` whose `process()` allocates nothing, takes no locks
and makes no system calls: MIDI arrives through a single-producer ring with
frame timestamps, the transport is rebuilt per block, outputs that are not
finite are zeroed and counted, and every block's time is measured against
the 128-frame budget. State is saved beside the plug-in in a small
checksummed file that survives the plug-in going missing. The SampQOL
adapter refuses to enable unless the stock app's build-id is in a table with
hardware-verified hooks, which no entry has.

## Honesty about the references

Melodee (AGPL) and MPC-FM (no licence) were studied, not copied;
`docs/AUDIT.md` records what each actually does and `docs/LICENSES.md` what
that means for this MIT project. No SampQOL, firmware or Steinberg file is
here or will be.

## Licence

MIT (`LICENSE`). Not affiliated with inMusic, Akai Professional, SampQOL
OS or Melodee. MPC and MPC Sample are trademarks of inMusic Brands, Inc.
VST is a trademark of Steinberg Media Technologies GmbH.
