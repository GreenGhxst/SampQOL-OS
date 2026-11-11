# SampQOL OS: free mod for the Akai MPC Sample

SampQOL OS is a free mod for the Akai Professional MPC Sample. It adds up to
16 drum tracks, a step sequencer, a mixer with inserts, 128-slice chop, stem
export and more to the stock app.

- It starts from the SD card. It does not flash the MPC.
- With the SD card in, SampQOL OS starts by itself. Without the SD card,
  the MPC starts as stock 1.3.0.
- Tested only with MPC Sample firmware 1.3.0. MPC One, Live and X are not
  tested yet.

**This is beta software. Back up your projects before you use it.**
A failed project load can delete samples. Read "Project safety net" in the
manual.

## Features

**Sequencing**
- Up to 16 drum tracks, each with its own 8 banks
- Step Edit: a Pad View with a step sequencer, and a Track View with one row for each track
- Record pad mutes and track mutes into a sequence. Each sequence keeps its
  own track mutes, as on the MPC Live
- Note Repeat, Cut and Paste in Step Edit
- Automate: fade the volume, low-pass or high-pass filter in or out
  (Linear, Log or S-Curve), erase all automation at once
- Select, name, explode and delete tracks and sequences
- Move a track: hold **PAD BANK** and push **-** or **+**

**Mixing and export**
- Mixer with inserts on pads, tracks, 8 submixes and the master (**PAD FX**)
- A 3-band kill EQ for each pad, with a live spectrum
- Export the mixdown and stems
- Resample one pad, with or without its Pad FX

**Sampling and chopping**
- Chop Mode with 128 slices, Link Slices and Extract
- Chop guard: after you edit slices by hand, a new Chop Type asks first.
  You do not lose your chops by accident.
- Sample Tail
- To Seq and Looper in Sample Record
- Preview samples with their own preview volume, also next to the sounds of
  your kit, and make new folders in Sample Select

**Playing**
- 16 Levels with Scale, Chord and Arp
- Keys mode for a MIDI keyboard, with a root note

**Workflow**
- Undo and Redo with a counter ("Undo 2/7")
- Settings page, FX Select List
- Template for a new project, random project names

This list shows only the main features. SampQOL OS has many more: see
[Manual and changes](#manual-and-changes).

## Buttons that work differently

- **SHIFT** + **SAMPLE** opens the Pad View. Stock: Input Config.
- **PAD FX** opens the Mixer. Stock: Pad FX.
- Pad FX: **SHIFT** + **PAD FX** (Flex Beat), then **B2**. Or set
  **Settings > Shortcuts > Shift+PAD FX** to Pad FX.
- Select a track: hold **PAD BANK** and turn the encoder.
- Or select a track with **SHIFT** + **PAD BANK** + pad 1-16.
- **Settings > Shortcuts** gives back the stock screens.

## Download

Download the zip from [Releases](../../releases). Use the file with the
highest version number. The release notes show the SHA256 of the zip.

Download SampQOL OS only from [Releases](../../releases) or from
[beatops.io](https://beatops.io). Do not upload or share the zip (see
[LICENSE](LICENSE)).

## Install

1. Make sure that the MPC Sample has firmware 1.3.0. Start the MPC
   **without** the SampQOL card. Push **SHIFT** + **PAD 8** (MIDI Config). The
   top right of the display must show **v1.3.0**. (When SampQOL OS runs, it
   shows the SampQOL OS version there.)
2. Unzip the zip file. Copy the folder `autorun` to the root of an exFAT
   microSD card.
3. Set the MPC off. Put the card in the MPC. Set the MPC on.
4. SampQOL OS starts by itself. You do not push a button.

Update from an earlier version: replace the folder `autorun` on the card with
the new one. Your settings stay: `mpcqol.conf` is on the top level of the
card, not in the folder `autorun`.

Go back to stock: set the MPC on without the card. To use the card on stock,
rename the folder `autorun` (for example to `autorun-off`), or move it into a
different folder on the card. Name it `autorun` again to start SampQOL OS.

If the MPC hangs at start-up: hold the power button until the MPC goes off,
then remove the card. See Troubleshooting in the
[manual](https://snarebes.github.io/SampQOL-OS/).

## Manual and changes

- [Manual](https://snarebes.github.io/SampQOL-OS/): all features, the safety
  net and troubleshooting. The zip also has the manual (`Manual.html`).
- [CHANGELOG](CHANGELOG.md): what changed in each version.

## Report a problem

Open an [issue](../../issues). Give the firmware version, the SampQOL OS
version and what you did.

## Source code

SampQOL OS is closed source for now. When Akai shares more about its open
firmware plans, I will decide about the source.

## License

SampQOL OS is freeware, not open source. See [LICENSE](LICENSE).

You may study and modify SampQOL OS for your own use. Made a modified version
and want to share it? Ask first: open an [issue](../../issues).

SampQOL OS (c) 2026 Snare Bes. All rights reserved.
Akai Professional, MPC and MPC Sample are trademarks of inMusic Brands, Inc.
SampQOL OS is not affiliated with, endorsed by or supported by inMusic
Brands, Inc. or Akai Professional.

## Experimental in this fork: FM1 synth (fm1mix)

The folder [`fm1`](fm1/) brings the open-source DX7 engine of the M-VAVE FM1
to the MPC Sample in two forms: `fm1mix`, an add-on for your own SampQOL OS
card (MIDI keyboard or SampQOL MIDI track plays it), and `fm1mod`, a
stand-alone mod on the Rust mpc-sample-sdk with pads, a screen panel and
voice selection on the encoder. Neither has run on a real MPC yet.
See [fm1/README.md](fm1/README.md).
