# Source audit (specification section 2)

Taken 2026-11-20 from fresh clones. Nothing from these repositories is
copied into this project; see `LICENSES.md` for why and for what was
reused as knowledge.

## Revisions examined

| Project | Repository | Commit | Date | Licence |
| --- | --- | --- | --- | --- |
| Melodee | github.com/keremimo/melodee-sample | `f11466589c7d28a3dd386c15a30743a6abc23fa3` | 2026-11-18 | AGPL-3.0-only (boot art all rights reserved) |
| sample-base (Melodee's runtime, submodule) | github.com/keremimo/sample-base | shallow clone of `main`, 2026-11-20 | 2026-11-20 | AGPL-3.0-only |
| MPC-FM | github.com/ThomasTestaud/mpc-fm | `a0f71138f279cbe76ef28840b39dc8e12d21d2e7` (0.2.0) | 2026-11-08 | **no licence file in the repository** |
| SampQOL OS | github.com/snarebes/SampQOL-OS | `cb7b914839a4f9d7a76be785e5b19b18e4a5a68a` | 2026-11-18 | proprietary freeware, closed source (LICENSE in repo) |

## Melodee: plug-in hosting reference

Melodee is not an add-on to the stock app. It is a complete replacement
groovebox built on `sample-base`, which owns the audio device (fixed
128-frame stereo callback at 44.1/48/96 kHz, AK4558 codec), the control
surface (UART3 raw MIDI at 4 Mbaud) and the SPI panel. Its plug-in code
sits in `src/melodee/plugin/`; the VST3, LV2 and CLAP hosts go through
JUCE (also AGPL), the VST2 path does not.

What its implementation actually does, per item of the specification:

- **VST2 plug-in discovery** (`plugin_files.cpp`): lists `Melodee/Plugins`
  on the card; a plain `.so`, an Open MPC folder (`plugin-meta.xml` beside a
  `.so`), `.vst3`, `.lv2`, `.clap`. For a `.so` it reads only the first 20
  bytes of the ELF header (`arm_elf()`): class, endianness, `e_machine`
  (40 = ARM32, 183 = AArch64). It does not parse `DT_NEEDED` or the symbol
  table at scan time; the entry point is checked by `dlopen` + `dlsym`
  (`Vst2Instance::exportsEntryPoint`), i.e. by loading the library.
- **ARM64 shared-library loading** (`vst2_instance.cpp`): `dlopen(RTLD_NOW |
  RTLD_LOCAL)`, then `dlsym("VSTPluginMain")` falling back to `"main"`.
  Before any plug-in loads, a "support pack" of stub GUI libraries (libGL,
  X11, xcb, cairo as no-op symbols) and real sound libraries the device
  lacks (libsndfile and codecs, libsamplerate, FFTW) is unpacked and
  preloaded (`plugin_support.cpp`, `tools/plugin_support/`). Its survey of
  40 ARM plug-ins (`SURVEY.md`) is the best public record of what real
  plug-ins need on this device: glibc 2.39, GLIBCXX 3.4.32, no X11/GL.
- **Audio plug-in lifecycle**: entry point → check `magic == 'VstP'` and
  `processReplacing` present → `effOpen`, `effSetSampleRate`,
  `effSetBlockSize`; at prepare: `effMainsChanged(1)`, `effStartProcess`;
  at release: `effStopProcess`, `effMainsChanged(0)`; destructor:
  `effClose`, `dlclose`. The host pointer is kept in `AEffect::resvd2`; a
  thread-local "opening" pointer serves callbacks made during the entry
  point. This project follows the same sequence (its own code).
- **MIDI event delivery**: one `VstEvents` with 512 preallocated
  `VstMidiEvent` slots per instance, filled from JUCE's `MidiBuffer` with
  `deltaFrames` = sample position; sent with `effProcessEvents` before
  `processReplacing`. 3-byte messages only; SysEx is dropped.
- **Parameter enumeration**: `numParams` from the struct; names, labels,
  display strings through opcodes 8, 6, 7 with a 256-byte buffer;
  `getParameter`/`setParameter` direct calls. A parameter's text can only
  be read for its *current* value in VST2.
- **Preset and state serialization**: `"MVS2"` tag + kind byte + either the
  `effGetChunk` bytes (when `effFlagsProgramChunks`) or every parameter as
  float; restored with `effSetChunk` or `setParameter`. Programs through
  `effGetProgram`/`effSetProgram`, names through opcode 29 then 5.
- **Processing block sizes**: fixed to the engine host's block (sample-base:
  128 frames); plug-ins are given exactly that at `effSetBlockSize`;
  `processBlock` clamps to it.
- **Sample-rate handling**: device rate given at open and prepare. Open MPC
  plug-ins assume 44.1 kHz, so `rate_bridge.h` resamples them
  (polyphase) when the device runs at another rate.
- **Latency and timing**: `initialDelay` reported through
  `setLatencySamples`; the engine host delays other paths to match (their
  "roadmap P4"). `audioMasterGetTime` returns a per-instance `VstTimeInfo`
  rebuilt each block from JUCE's play head (ppq, bar start, tempo, time
  signature, playing/changed flags).
- **Plugin unloading**: `dlclose` only in the destructor, after
  `releaseResources`; the rack owns instances on its own thread and hands
  the engine a `PluginRunner` interface, so the audio thread never sees a
  plug-in being destroyed.
- **ARM32 bridging** (`vst2_bridge_instance.*`, `vst2_bridge_protocol.h`,
  `bridge32/vst2_bridge32.cpp`): a separate 32-bit process
  (`vst2_bridge32`, built with `arm-linux-gnueabihf-g++` and shipped with
  Debian bookworm's armhf loader, glibc, libstdc++) maps one `memfd` shared
  block (fixed-width struct: command, 16×4096 float in/out, 512 events,
  1 MB data) and handshakes with two futex words (`request`/`reply`). The
  host writes a command, bumps `request`, and waits for `reply` in the same
  audio period (no added latency); the bridge's worker inherits the audio
  thread's SCHED_FIFO priority. A crash in the plug-in kills the bridge, not
  the host. This is the design this project would follow for ARM32 (see
  section 11 of the specification); it is deferred.
- **Error and crash handling**: load errors are strings on the picker
  ("Could not open"); unanswered host opcodes are recorded per instance for
  a probe tool (`tools/vst_probe`). In-process plug-ins have no crash
  isolation; only the 32-bit bridge does. sample-base restores the stock
  app on crash (`install/restore-stock.sh`).

Conclusion: Melodee proves that aarch64 VST2 `.so` plug-ins load and run on
the MPC Sample's Linux, that the device audio block is 128 frames, and what
system libraries real plug-ins need. Its code cannot be imported here
without putting this project under the AGPL; it was read, not copied.

## MPC-FM: SampQOL add-on integration reference

MPC-FM runs *inside* the stock app next to SampQOL's `mpcqol.so`. Files
read: `src/fm.c`, `src/voice_hook.c`, `src/discover.c`, `src/alsa_probe.c`,
`src/crash.c`, `launcher/setarch.c`, `launcher/autorun.sh.in`,
`launcher/autorun.json`, `tools/*.sh`, `tests/test_autorun.sh`, `Makefile`.

How it gets loaded (the part that is a verified, reusable *technique*):

1. The stock script runner starts `autorun/autorun.sh` from the card when
   `autorun.check == sha256(autorun.json || autorun.sh || SALT)`; the salt
   is a firmware constant documented in the orrpunk-art root-SSH notes.
2. MPC-FM's `autorun.sh` finds SampQOL's unchanged official folder (under
   another name) and `exec`s SampQOL's own `autorun.sh`.
3. SampQOL's `qol-run.sh` starts the app as
   `LD_PRELOAD=/tmp/mpcqol.so setarch -R -- "/usr/bin/MPC Sample"`.
   MPC-FM puts a static `setarch` stand-in first in `PATH`; it appends its
   own `.so` to `LD_PRELOAD` only when the preload already names
   `mpcqol.so`, applies a crash guard (state in tmpfs: at most 3 starts per
   boot, a stop within 600 s of a start disables it until reboot), draws a
   status line over the splash, then `execv`s the real `setarch`.
4. The preloaded library stays passive unless `/proc/self/exe` is the stock
   app; it reads the app's GNU build-id with `dl_iterate_phdr` and only
   installs hooks for one known build (`1dac7f48…`, fw 1.3.0.12).
5. Files are pinned by SHA256 in `autorun.sh`; a `DISABLE` file turns it
   off; the card is `noexec`, so binaries are copied to `/run` first.

How it generates sound (the part that is NOT reusable for a generic host):

- `voice_hook.c` rewrites three entries of the vtable of the stock
  `DefaultSampleGenerator` class (render, stop, start) at fixed offsets of
  one app build, after checking all 16 entries are stock. The render hook
  lets the stock code run, then overwrites the layer's mono buffer with FM
  × the stock gain ramp, derives pitch from how far the stock read position
  moved, and writes the position back. It needs `discover.c` to search the
  heap at run time (`process_vm_readv`) for the sample's frames, because
  the data sits behind several unknown objects.
- Everything is tied to one pad *having a sample loaded*: an FM "carrier"
  WAV whose first 336 frames hide the patch in the low 4 bits. The hook
  only fires for mono, non-time-stretched layers. Stereo output,
  note-off handling, velocity → timbre and a true polyphonic voice model
  are listed as limits in its own INSTALL.txt.
- The hook runs on the stock audio thread inside the voice render;
  budget per 128-frame block is 2.9 ms (its `alsa_probe.c` measures this).

Can this integration carry a generic plug-in? Not as it stands:

1. A VST2 instrument renders *stereo* blocks for *all* its voices at once,
   driven by MIDI note on/off; the hook point is *one mono layer of one
   voice*, called once per sounding layer, with no note-off signal
   (`stop` fires when the stock envelope ends, not when the pad is
   released) and with pitch only as a ratio of a read position. Mapping
   a polyphonic synth onto per-layer mono render calls would mean rendering
   the plug-in once per block in one layer and silencing the others,
   inventing a note-off from the stock envelope, and losing stereo.
2. The hook addresses are for app build `1dac7f48` only and are not
   verified by this project on hardware. MPC-FM itself labels 0.2.0 a
   prototype and its page "not yet tested on the device".
3. There is no licence file in the MPC-FM repository. Without one, its code
   is all-rights-reserved by default and must not be copied. The launcher
   *mechanism* (PATH stand-in, LD_PRELOAD append, crash guard, build-id
   gate) is a technique, not an expression, and is what this project's
   `sampqol_compat` module and the planned launcher follow in their own
   code.

So the verdict the specification asks for: MPC-FM demonstrates a safe way
to *load* a library beside SampQOL and a safe way to *fail closed*, but its
audio integration is specific to its FM carriers and cannot be reused as a
generic plug-in audio path. The `alsa_probe.c` approach (interposing
`snd_pcm_writei` and friends to *measure* the stock app's block timing) is
the one hook that is generic and harmless, and the natural first step for
finding a real integration point (see `SAMPQOL_INTEGRATION.md`).

## SampQOL OS: compatibility target

Read: `README.md`, `LICENSE`, `CHANGELOG.md` and the manual `index.html` at
the revision above.

- Installation: the release zip's `autorun` folder at the root of an exFAT
  card; `mpcqol.conf` at the card root. Tested only with MPC Sample
  firmware 1.3.0 (app 1.3.0.12 per MPC-FM and Melodee). It "does not flash
  the MPC"; without the card the MPC is stock.
- Boot: the stock script runner → `autorun.sh` → `qol-run.sh` (watchdog and
  "project safety net") → the stock app with `LD_PRELOAD=/tmp/mpcqol.so`.
  Current version 0.1.5 (1 November 2026).
- Licence: freeware, closed source. Use, study and modify for own use are
  allowed; uploading, hosting or distributing SampQOL or a modified copy is
  not without written permission. The manual says nothing about plug-ins,
  VST or any hosting API, and there is no public interface for a third
  party to feed audio or MIDI into it. SampQOL version identification: no
  published API; the splash and MIDI Config screen show the version.
- Consequence for this project: never bundle `mpcqol.so` or any SampQOL
  file; detect SampQOL at run time (its `.so` in the process); treat its
  internals as unknown; keep the stock `autorun` chain intact when the
  expansion is disabled or absent.
