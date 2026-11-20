# SampQOL integration: findings and the blocker (specification sections 5, 6, 15 v0.3)

## Where things stand

The standalone host (v0.1 and v0.2 of the milestones) is implemented and
tested offline. The SampQOL compatibility adapter is implemented **as a
gate only**: `src/sampqol_compat/` detects the process it runs in, the
stock app's GNU build-id, whether SampQOL's `mpcqol.so` is loaded, the
`DISABLE` file and the crash guard, and answers "disabled" with a reason
unless every check passes **and** the compatibility table marks the build's
audio and MIDI hooks as verified. No entry is marked verified. There is no
runtime hook code in this repository, and nothing here is injected into the
stock app.

The specification says: *"If a stable integration point is unavailable,
report the limitation rather than force an unverified hook."* This document
is that report.

## What was looked for

A way for a plug-in instrument to be the sound source of a pad or track
with the normal sequencing and hardware workflow preserved (specification
section 6), i.e. three things inside the running stock app:

1. **Pad/MIDI in**: learn, on the audio thread or just before it, that a
   pad (or a sequencer step on a track) wants note N at velocity V on, and
   later off.
2. **Audio out**: write a stereo block into the sampler's own processing
   path for that pad or track, so the stock filter, envelope, Pad EQ,
   inserts, mixer, 16 Levels, Keys, resample and export apply.
3. **Timing**: do (1) and (2) at the stock block rate, inside the stock
   block budget, without allocating or blocking.

## What the references actually provide

### MPC-FM (the only existing add-on inside SampQOL)

`voice_hook.c` swaps three vtable entries of the stock
`DefaultSampleGenerator` (render / stop / start) for one app build. That
gives a callback **per sounding layer of a drum voice**, mono, with a float
buffer and a frame count, and the stock gain ramp. It is a sample-replacement
hook: the stock engine still thinks it plays a WAV. For MPC-FM's own synth
that is enough because its carrier WAV defines the note length and pitch and
one layer equals one FM voice.

For a VST2 instrument it is not an integration point:

| Needed | What the hook gives | Gap |
| --- | --- | --- |
| Note on with velocity | `start` on a generator whose sample happens to be a carrier | Works only if a pad has a *sample* loaded; velocity only as the stock gain |
| Note off | nothing (the `stop` call is the stock envelope ending) | A VST note would ring until the stock amp envelope cut it, or be cut when the plug-in still sustains |
| Polyphony | one render call per layer, each expecting *that layer's* samples | A synth renders all its voices in one call; feeding each layer separately would multiply the work and still not know which voice is which |
| Stereo | mono layer buffer (stereo layers go to another render function the hook declines) | Mono only |
| Pitch | ratio of a 32.32 read position moved by the stock engine | Usable as a pitch *hint*; not a MIDI note |
| Verified on hardware | by MPC-FM's author for its own use, labelled prototype | not by this project |

Conclusion: MPC-FM proves the *loading* path, not an *audio* path for a
generic plug-in. Its licence situation (no licence file) also forbids
copying its hook code, offsets included.

### Melodee / sample-base

Melodee does not run inside the stock app at all; it replaces it and owns
the ALSA device. Its plug-in host is therefore of no help for routing audio
*through* SampQOL, only for how to host a plug-in (which this project now
does on its own).

### SampQOL OS

Closed source, no plug-in or audio API, no documentation of any extension
point. Its own hooks into the stock app (16 drum tracks, inserts, EQ) show
that the stock engine can be extended at the binary level, but how it does
so is not published, and the licence forbids redistributing modified copies.

## What would have to be demonstrated before enabling the adapter

In this order, each on hardware, each logged:

1. **Passive measurement** (harmless, generic): a preload library that only
   interposes `snd_pcm_writei`/`snd_pcm_mmap_commit` to record the real
   block period, block size and headroom of the stock app with SampQOL
   running, exactly as MPC-FM's `alsa_probe.c` does. This project's
   `PerfMonitor` is written to consume those numbers. Expected (from
   MPC-FM's notes): 128-frame blocks, 2.9 ms period at 44.1 kHz.
2. **A genuine note-on/note-off source**: the stock app reads the control
   surface as raw MIDI on UART3 (sample-base `HARDWARE.md`; MPC-FM's
   `surface.c` interposes `snd_rawmidi_read` to *filter* it for its own
   page). Interposing the read is a documented, generic hook that yields
   pad hits with velocity and releases without touching the voice engine.
   It must be shown that reading the stream does not steal or delay the
   app's own pad handling (MPC-FM passes every byte through; so would we).
3. **An audio injection point that is a mix bus, not a sample layer**:
   candidates are (a) a pad whose sample is a known silent WAV, hooked as
   MPC-FM does, but used only as a *trigger and gain carrier* while the
   plug-in renders into that layer once per block, with the other layers of
   the same plug-in muted (loses stereo and note-off; weak); (b) the output
   of the stock mixer just before `snd_pcm_writei`, where a stereo float or
   S32 block could be summed in (preserves nothing of the stock per-pad
   chain, but is generic, stereo and verified by the probe in step 1);
   (c) a per-track insert point inside SampQOL's own mixer, which only its
   author can expose. (b) is the only one this project can verify without
   SampQOL internals, and it is what a first "experimental, post-mixer"
   mode would use, clearly labelled as not going through the pad chain.
4. **Crash guard and disable path** (already implemented as logic here)
   proven on the device: `DISABLE` file, three-start limit, 600-second rule.

Until step 3 produces plug-in audio from the device's own output with the
stock app and SampQOL running normally, the adapter stays closed and the
status matrix (`STATUS.md`) keeps "SampQOL-integrated: no".

## What is deliberately not in this repository

- No `autorun.sh`, `autorun.json`, `autorun.check` or `setarch` stand-in. A
  launcher that chains into SampQOL's boot is only worth shipping together
  with a verified payload; an untested one adds a boot-time failure mode for
  nothing. The mechanism is documented in `AUDIT.md` and the compat gate is
  ready to be called from such a launcher's preload library.
- No vtable offsets, class names or binary patches of the stock app.
- No SampQOL files.

## How to pick this up

1. Build the aarch64 package (`./build.sh package`), copy `VST-Expansion`
   to the card, run `Host/run-selftest.sh` over root SSH on the device:
   this is the first hardware test of the host itself.
2. Write the passive ALSA probe preload (step 1 above) in a separate,
   clearly experimental folder; load it with the MPC-FM technique
   (own `autorun` folder, SampQOL's folder unchanged beside it); confirm
   SampQOL starts and plays normally with the probe in place; read the
   timing log.
3. Only then design the audio injection against measured facts.
