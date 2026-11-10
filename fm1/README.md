# fm1mix: the open FM1 synth inside SampQOL OS on the MPC Sample

fm1mix adds the DX7 engine of the open-source M-VAVE FM1 firmware
([m-vave/fm1_open](https://github.com/m-vave/fm1_open)) to your own SampQOL OS
card. It runs inside the stock MPC Sample app, next to `mpcqol.so`, and mixes
its sound into the MPC's audio output. Nothing is flashed. Like SampQOL OS, it
lives in RAM and is gone when the MPC is off.

**Status: built and tested in a simulated MPC app on x86 and on aarch64 under
QEMU. It has NOT been run on a real MPC Sample yet.** Read "What is verified
and what is not" before you put it on a card.

## What you get

- The FM1 DX7 engine (128 FM voices, 6 operators, fixed point) with the
  original FM1 V15 factory bank of 128 voices, 8-voice polyphony by default.
- It is played by:
  - a MIDI keyboard on the MPC's MIDI IN or USB (as set in MIDI Config),
  - a SampQOL OS MIDI track with its own MIDI Out channel, so the MPC
    sequencer plays the synth,
  - scripts, through `/tmp/fm1.cmd` and `/tmp/fm1.midi`.
- MIDI Program Change selects a voice, CC 7 sets the volume, CC 1 is the
  mod wheel, CC 64 sustain, pitch bend (2 semitones).
- A DX7 bank of your own (32-voice SysEx dump) for voices 1 to 32.

What you do not get: the rest of the FM1 (303, 808, sampler, sequencer, its
screen). The FM1's own engine is a closed binary for a different CPU and is
not used; only the C sources of the open DX7 engine are.

## How it works

The MPC Sample is a Linux computer. SampQOL OS starts the stock app with
`LD_PRELOAD=mpcqol.so`, which wraps the app's ALSA calls. fm1mix is a second
preload library (`fm1mix.so`) in the same list. It wraps:

| ALSA call | fm1mix does |
|-----------|-------------|
| `snd_pcm_writei` / `snd_pcm_writen` | renders the synth and adds it to the audio the app writes (S16, S24, S32 or float, any rate) |
| `snd_rawmidi_read` | reads along: MIDI IN and USB MIDI play the synth |
| `snd_rawmidi_write` | reads along: MIDI the MPC sends (SampQOL MIDI tracks) plays the synth |
| `snd_pcm_close` | forgets the device |

The patch to the SampQOL scripts is 10 lines in `qol-run.sh` (load the
library if `autorun/fm1/fm1mix.so` exists, copy the log to the card) and the
new checksum of `qol-run.sh` in `autorun.sh`. See
[patch/sampqol-0.2.0-fm1.patch](patch/sampqol-0.2.0-fm1.patch).

Removing `autorun/fm1/fm1mix.so` from the card switches the synth off without
undoing the patch.

## Build

On Linux (Ubuntu or Debian):

```
sudo apt install build-essential gcc-aarch64-linux-gnu
# for `make test` also: qemu-user libasound2-dev python3
cd fm1
make          # -> build/fm1mix.so (aarch64, for the MPC Sample)
make test     # host + aarch64 tests in a simulated app
```

The device library is linked without a C library, so it needs no particular
glibc version. The build fails if an import outside the glibc 2.17 API shows
up, or if a symbol version is required.

## Install on your card

1. Download the SampQOL OS zip from the official source (0.2.0 was used
   here). Unzip it. You get a folder `autorun`.
2. Patch your copy:
   ```
   sh patch/apply.sh /path/to/autorun build/fm1mix.so
   ```
   Optional: `sh patch/apply.sh /path/to/autorun build/fm1mix.so my.conf mybank.syx`.
   The script keeps the originals in `autorun/fm1/orig`, inserts the lines,
   updates the checksum in `autorun.sh`, and runs the same checksum test that
   `autorun.sh` runs on the MPC.
3. Edit `autorun/fm1/fm1.conf` if you like (see [card/fm1.conf](card/fm1.conf)).
4. Copy the folder `autorun` to the root of the exFAT card, as in the
   SampQOL OS manual. Put the card in, set the MPC on.

Undo: `sh patch/revert.sh /path/to/autorun`, or copy the original `autorun`
from the zip again.

## Play

- **MIDI keyboard:** MIDI Config (SHIFT + PAD 8): set MIDI Port to the port
  your keyboard is on. Everything the keyboard sends also reaches the synth.
  Set `channel` in `fm1.conf` if you want only one channel to play it.
- **From the sequencer:** give a SampQOL MIDI track its own MIDI Out channel
  (SampQOL OS 0.2.0, Track MIDI Out). Set `channel` in `fm1.conf` to that
  channel so the drum tracks' notes do not play the synth.
- **Voice:** `program = 1..128` in `fm1.conf`, or MIDI Program Change.
- **Volume:** `volume`, `gain` in `fm1.conf`, or CC 7. The synth is added to
  the MPC's own mix; keep headroom or the master clips.
- **Scripts:** `echo "prog 12" > /tmp/fm1.cmd`, `echo "on 60 100" > /tmp/fm1.cmd`,
  `echo "status" > /tmp/fm1.cmd` (writes to the log). Raw MIDI bytes go to
  `/tmp/fm1.midi`. Commands: `prog N`, `vol N`, `ch N`, `mono 0|1`,
  `transpose N`, `on NOTE VEL`, `off NOTE`, `midi 90 3C 64`, `panic`, `status`.

## What is verified and what is not

Verified in this repository (`make test`):

- The engine renders the factory voices; silence when no note plays; sound
  after note on from MIDI IN, from MIDI OUT, and from the FIFO; the channel
  filter; a second pcm with another rate and format (S32 interleaved at
  44.1 kHz, S16 non-interleaved at 48 kHz); the app's own audio passes
  through unchanged when the synth is silent; no clipping.
- The same with a second interposing library in front of fm1mix, as
  `mpcqol.so` will be.
- The aarch64 library loads and runs under QEMU user emulation with the
  Ubuntu aarch64 glibc, and imports only glibc 2.17 API without versions.
- `apply.sh` on the 0.2.0 release: the pin check of `autorun.sh` passes,
  `revert.sh` restores the folder byte for byte.

Not verified, because no MPC Sample was available:

- That the stock app writes audio with `snd_pcm_writei`/`writen`.
  `mpcqol.so` wraps exactly these calls, so this is very likely. If the app
  used mmap access instead, you would hear nothing and the log would show no
  "FM1 mixes into this stream" line.
- That MIDI IN reaches the app through `snd_rawmidi_read`. If the app uses
  the ALSA sequencer, the `midi_in` tap sees nothing; `midi_out` (SampQOL
  MIDI tracks) and the FIFOs still work.
- The file `autorun/autorun.check` in the release. Nothing in the SampQOL
  scripts or in `mpcqol.so` reads it, and its value is not the SHA-256 of any
  file in the folder. If the stock firmware checks it against `autorun.sh`,
  the patched folder will not start and the MPC boots stock: then revert.
- The MPC's glibc version. The library needs nothing newer than glibc 2.17
  (`mpcqol.so` itself needs 2.30).
- CPU load. The DX7 engine is fixed point and was written for a small
  microcontroller; 8 voices should be a small load on the MPC Sample's
  ARM CPU. Lower `voices` if the MPC's audio stutters.

If the MPC shows the SampQOL logo but the synth is silent, read
`autorun/fm1/fm1mix.log` on the card (copied there each time the app exits,
also at a crash) or `/tmp/fm1mix.log` on the MPC.

## Safety

- fm1mix runs inside the app's audio thread. A bug there stops the app.
  SampQOL OS restarts the mod; after two stops within two minutes it starts
  stock. Your project is covered by the SampQOL project safety net.
- Back up your projects before you try this. This is beta on top of beta.
- Do not contact Akai or the SampQOL OS author about a patched card. Remove
  `autorun/fm1` and run `revert.sh` first, then see if the problem stays.

## Licenses

- `shim/`, `test/`, `patch/`: GPL-3.0-only (this fork).
- `engine/fm1_open/`: unchanged files of m-vave/fm1_open, commit
  `4c7496d`: the DX7 core is Apache-2.0 (Google MSFA, modified by M-VAVE),
  the rest of the engine is GPL-3.0 with the FM1 SDK linking exception or MIT,
  the factory bank is Apache-2.0. See
  [engine/fm1_open/NOTICE.md](engine/fm1_open/NOTICE.md).
- SampQOL OS is freeware by Snare Bes: you may modify it for your own use,
  but you may not upload or share a modified `autorun` folder. Share this
  repository and a link to the official SampQOL OS download instead.
- Akai Professional, MPC and MPC Sample are trademarks of inMusic Brands,
  Inc. M-VAVE and FM1 are trademarks of their owner. This project is not
  affiliated with either.
