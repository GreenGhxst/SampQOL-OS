# The open FM1 synth on the MPC Sample

This folder brings the DX7 engine of the open-source M-VAVE FM1 firmware
([m-vave/fm1_open](https://github.com/m-vave/fm1_open)) to the Akai MPC Sample
(firmware 1.3.0.12). Nothing is flashed: like SampQOL OS, everything starts
from the SD card, lives in RAM and is gone when the MPC is off. There are two
builds:

| | **fm1mix** (`shim/`) | **fm1mod** (`mod/`) |
|---|---|---|
| Runs with | SampQOL OS 0.2.0, as a second preload next to `mpcqol.so` | the stock app alone, as its own `autorun` zip |
| Built with | C, no dependencies | Rust on [mpc-sample-sdk](sdk/) (typed hooks into the app) |
| Pads play it | no | yes, 16 pads chromatic, velocity, octave with + / - |
| Screen | no (status in a log) | panel on the Sample page: voice, volume, octave, voices |
| Voice select | MIDI Program Change, `fm1.conf`, FIFO | DATA encoder, MIDI Program Change |
| MIDI keyboard | yes (tap on the app's MIDI read) | yes (the app's own note hook) |
| Sequencer | SampQOL MIDI tracks (tap on MIDI out) | no |
| Tested | simulated app on x86 and aarch64 (QEMU) | host unit tests; builds and packs |

**Neither has run on a real MPC Sample yet.** Read "What is verified and what
is not" before you put either on a card. fm1mod and SampQOL OS cannot be used
at the same time: both patch the same functions inside the app (see below).

## fm1mod: pads and a screen, without SampQOL OS

```
cd fm1/mod && ./build.sh        # -> build/fm1mod.so and build/fm1mod.zip
```

Unzip `build/fm1mod.zip` and copy its `autorun` folder to the root of an
exFAT card (not next to a SampQOL `autorun`: one card, one `autorun`). The
generated `autorun.sh` checks the firmware, copies the library to `/run`,
adds an `LD_PRELOAD` drop-in for the app's service and restarts the app.
Remove the folder and reboot to go back to stock.

On the MPC:

- **SHIFT + DATA** (push the encoder) opens and closes the FM1 panel. The
  panel is drawn on the Sample page.
- While it is open the **16 pads** play the synth from C3 up (pad 1 = C3),
  with velocity. **+ / -** move the pads an octave. The pads do not play
  their samples while the panel is open; close it to get them back.
- **DATA** turn selects one of the 128 FM1 factory voices. **SHIFT + turn**
  sets the volume.
- A **MIDI keyboard** (MIDI IN or USB, as set in MIDI Config) plays the synth
  while the panel is open; those notes do not reach the pads.
- The status line at the bottom shows voice, volume and octave.

Build needs: Rust 1.97 with `rustup target add aarch64-unknown-linux-gnu`,
`gcc-aarch64-linux-gnu`, `llvm` (objcopy, objdump), `python3`, `zip`.
`cargo test` in `mod/` runs the engine and queue tests on the host.

Why not together with SampQOL OS: the SDK hooks 17 functions inside the app by
patching their first instruction, and refuses to install unless the original
words are still there. `mpcqol.so` hooks the same app, so whichever loads
second will see patched code. Use fm1mix with SampQOL OS, fm1mod without it.

## fm1mix: inside SampQOL OS

### What you get

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

### How it works

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
library if `autorun/fm1/fm1mix.so` exists, copy the log to the card), the
new checksum of `qol-run.sh` in `autorun.sh`, and a new `autorun.check`. The
stock firmware runs `autorun.sh` only when `autorun.check` equals the SHA-256
of `autorun.json` + `autorun.sh` + a fixed UUID (the mpc-sample-sdk build
script documents this; the SampQOL release matches it). `apply.sh` rewrites
it. See [patch/sampqol-0.2.0-fm1.patch](patch/sampqol-0.2.0-fm1.patch).

Removing `autorun/fm1/fm1mix.so` from the card switches the synth off without
undoing the patch.

### Build

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

### Install on your card

1. Download the SampQOL OS zip from the official source (0.2.0 was used
   here). Unzip it. You get a folder `autorun`.
2. Patch your copy:
   ```
   sh patch/apply.sh /path/to/autorun build/fm1mix.so
   ```
   Optional: `sh patch/apply.sh /path/to/autorun build/fm1mix.so my.conf mybank.syx`.
   The script keeps the originals in `autorun/fm1/orig`, inserts the lines,
   updates the checksum in `autorun.sh`, rewrites `autorun.check`, and runs
   the same checksum test that `autorun.sh` runs on the MPC.
3. Edit `autorun/fm1/fm1.conf` if you like (see [card/fm1.conf](card/fm1.conf)).
4. Copy the folder `autorun` to the root of the exFAT card, as in the
   SampQOL OS manual. Put the card in, set the MPC on.

Undo: `sh patch/revert.sh /path/to/autorun`, or copy the original `autorun`
from the zip again.

### Play

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

Verified in this repository (`make test` for fm1mix, `cargo test` for fm1mod):

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
  `autorun.check` matches the firmware's formula, `revert.sh` restores the
  folder byte for byte.
- fm1mod: the engine renders the factory voices through the Rust wrapper,
  program changes switch voices, the MIDI queue keeps order; the aarch64
  build passes the SDK script's checks (no unresolved imports, glibc at most
  2.39) and packs into an `autorun` zip.

Known from the SDK (mpc-sample-sdk, which the minimod project runs on real
devices): the app resolves `snd_pcm_writei` by name, so a preload library can
wrap it (that is how the SDK itself mixes into the output); the stream is
S32; the device glibc is 2.39; the firmware is 1.3.0.12 with the first-4KiB
hash that SampQOL OS also checks.

Not verified, because no MPC Sample was available:

- That MIDI IN reaches the app through `snd_rawmidi_read` (fm1mix). If the
  app uses the ALSA sequencer, the `midi_in` tap sees nothing; `midi_out`
  (SampQOL MIDI tracks) and the FIFOs still work. fm1mod does not depend on
  this: it uses the SDK's note hook.
- The scale of the pad and MIDI velocity the SDK reports (fm1mod handles
  both 0..1 and 0..127).
- That the SDK's 17 hooks install next to nothing else; the emulator in
  [sdk/docs/23-autonomous-development.md](sdk/docs/23-autonomous-development.md)
  is the place to check this before a device.
- CPU load. The DX7 engine is fixed point and was written for a small
  microcontroller; 8 voices should be a small load on the MPC Sample's
  ARM CPU. Lower `voices` if the MPC's audio stutters.

If the MPC shows the SampQOL logo but fm1mix is silent, read
`autorun/fm1/fm1mix.log` on the card (copied there each time the app exits,
also at a crash) or `/tmp/fm1mix.log` on the MPC. If fm1mod shows no
"FM1 synth" status line after the restart, the SDK refused to install (wrong
firmware or patched code); nothing else happens then.

## Safety

- fm1mix runs inside the app's audio thread. A bug there stops the app.
  SampQOL OS restarts the mod; after two stops within two minutes it starts
  stock. Your project is covered by the SampQOL project safety net.
- Back up your projects before you try this. This is beta on top of beta.
- Do not contact Akai or the SampQOL OS author about a patched card. Remove
  `autorun/fm1` and run `revert.sh` first, then see if the problem stays.

## Licenses

- `shim/`, `mod/`, `test/`, `patch/`: GPL-3.0-only (this fork).
- `sdk/`: mpc-sample-sdk 0.1.9 by Jakub Kolcar, MIT, unchanged
  ([sdk/NOTICE.md](sdk/NOTICE.md)).
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
