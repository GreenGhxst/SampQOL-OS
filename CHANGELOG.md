SampQOL OS Release Notes
========================

SampQOL OS is a mod for the MPC Sample firmware 1.3.0. It starts from the
SD card and does not flash the device. Without the SD card, the MPC starts
as stock 1.3.0.

The notes of version 0.1.4.7 show all that SampQOL OS does differently
from stock 1.3.0. Each later version shows what changed.
See the MANUAL for how to use each function.

A SampQOL project opens on a stock MPC Sample, but the mod functions (more
tracks, submixes, inserts, EQ, mutes) do not work there.
Tested only with MPC Sample 1.3.0. MPC One, Live and X are not tested yet.

SampQOL OS (c) 2026 Snare Bes. All rights reserved. See LICENSE.txt.
Akai Professional, MPC and MPC Sample are trademarks of inMusic Brands, Inc.


Version 0.1.5 (1 November 2026)
-------------------------------

HIGHLIGHTS

- Track mutes per sequence, as on the MPC Live/One/X etc.
- Hold PAD BANK and push - or +: the current track moves up or down in the track order.
- Note Repeat in Step Edit.
- Step Edit: cut notes and paste them in another place.
- Sample Select: pads that have a sample can play their own sound next to
  the preview (Own Sample).
- Resample Pad can also print the Pad FX (On / Off).

SEQUENCES AND TRACKS

- Track mutes per sequence, as on the MPC Live: each sequence keeps its own
  track mutes. A song that you export to a sequence keeps the track mutes
  of each part.
- Hold PAD BANK and push - or +: the current track moves one place up or
  down. One UNDO moves it back. This works on all screens.

STEP EDIT

- Note Repeat works in Step Edit: in the Pad View (also with Steps on) and
  in the Track View.
- Step Edit: cut notes and paste them in another place. Pad View: Cut
  (SHIFT + B1). Track View: Copy the notes, then ERASE them.

MIXER AND EFFECTS

- Ch. Mixer: Mute on a track strip and Track Mute are one mute. The strip
  shows Mute also when the sequence plays a recorded track mute.
- Pad FX: Fixed effects stay on when you leave the Pad FX screen, as Latch.
- SHIFT + PAD FX can open the stock Pad FX screen at once (Settings >
  Shortcuts). The default is Flex Beat, as stock.
- FX Select and the Insert FX list show an icon for each effect group.

KNOBS

- Sample Tail and 16 Levels Scale: the encoder changes the value of the last
  knob that you turned, as on the EQ page.

SAMPLE RECORD AND RESAMPLE

- Resample Pad: also prints the Pad FX that are on (Fixed or Latch). You set
  the Pad FX print and the Knob FX print On or Off. SHIFT + K1 sets the tail
  (0 to 10 seconds) when an effect is in the new sample.

LOADING AND SAVING

- Sample Select: a pad press selects the pad and previews the file at
  once. SHIFT + K2 "Own Sample": pads with a sample play their own sound, so
  you can compare files with your kit. B3 is the stock auto-preview.
- Sample Select: New Folder also works in an empty folder.
- Browsers: the FADER scrolls the list, as a scrollbar.

FIXED

- Load Kit and Load Project: two drum tracks with the same name lost notes
  when you saved. Each track gets its own name (for example "Drum 001-2")
  and keeps its notes.
- Load Kit, then UNDO: the next save could crash when two tracks had the
  name of the kit.
- Half Seq and Double Seq (SHIFT + PAD 2 / 3) change all drum tracks. They
  changed only track 1.
- Track Mute: the pad of a muted track is light red. It was off, as a pad
  without a track.
- Pad EQ: a kill also stops the attack of a new note. The first part of a
  snare or a clap went through the kill.
- MAIN VOLUME works in the Pad View and the Track View. It did nothing
  there, or it changed the steps.
- Step Edit keeps the cursor, the selection and the copied notes when you
  open a stock screen (for example Resample or the FADER menu) and go back.
  The Pad View opened again at step 1.
- Track View: Paste after Copy and ERASE puts the notes back in the same
  place. Paste did nothing there.
- Track View: Copy and Duplicate take up to 8192 notes. With more than 512
  notes, they copied only a part. With more than 8192 notes, the MPC shows
  "Too many notes".
- The folder pages (Home Folder, Save Sample, Save Project, Export) go up
  to the root of the SD card, as Sample Select. They stopped in the folder
  MPC-Sample.
- Sample Select: a push on the selected pad on a folder does not open
  New Folder.
- Sample Select: Prev. Vol. does not cover a long folder name, and does
  not show on a folder.
- Sample Select: New Folder (B3) shows as a stock button, not as the
  Preview box.

KNOWN ISSUES

- Battery Monitor: the charge is an estimate from a voltage. It can stay at
  one value (for example 45%) and not follow the battery. The stock battery
  light shows the correct state.
- A stock MPC Sample does not play the track mutes in a sequence that you
  made with Song Export. All tracks sound there.

Version 0.1.4.9 (31 October 2026)
---------------------------------

START-UP AND SAFETY

- With the SD card in, SampQOL OS starts by itself at start-up, without a
  question. Without the SD card, the MPC starts as stock 1.3.0.
- Stock with the SD card in: rename the folder "autorun" (for example to
  "autorun-off"), or move it off the top level of the card. See the MANUAL,
  Troubleshooting.

FIXED

- FULL LEVEL (SHIFT + PAD 1) works in the Pad View and the Track View: each
  new note from a pad has full velocity.


Version 0.1.4.8 (31 October 2026)
---------------------------------

SEQUENCES AND TRACKS

- Explode: a pad with Out Master goes to Out Track on its new track, so
  the track strip meters, mutes and sets its volume.

MIXER AND EFFECTS

- Mixer: B1 shows the submix that the current pad (or its track) goes to.

FIXED

- The SD card works with each card name. In 0.1.4.7 a card with a name
  other than "MPC" (for example "MPC SAMPLE", the name the MPC gives when it
  formats a card) showed "No SD card" on the folder pages, saved projects
  and exports on Internal, and did not keep the Settings.
- Mixer: the Ch. Mixer and Pad Mixer screens do not flicker during
  playback.


Version 0.1.4.7 (30 October 2026)
---------------------------------

HIGHLIGHTS

- Up to 16 drum tracks in a project.
- Step Edit: a Pad View with a step sequencer, and a Track View with one
  row for each track.
- Mixer: inserts on pads, tracks, 8 submixes and the master.
- Pad EQ: a 3-band kill EQ for each pad, with a live spectrum.
- Chop Mode: up to 128 slices, and Extract to pads without a new WAV.
- Record pad mutes and track mutes.
- A MIDI keyboard plays the selected pad chromatically.
- Sample Select: make a new folder on the SD card.
- An Undo counter, and Undo on SAMPLE RECORD.
- Sample Record: To Seq puts the take in the sequence, and a Looper records
  more passes.
- Export: a mixdown and stems of a sequence as WAV.

BUTTONS THAT WORK DIFFERENTLY

- SHIFT + SAMPLE opens the Pad View (Step Edit). Stock: Input Config.
- PAD FX opens the Mixer. Stock: Pad FX.
- Settings > Shortcuts gives back the stock screens.
- Select the track: hold PAD BANK and turn the encoder, or push
  SHIFT + PAD BANK + a pad (1-16).
- Chop Mode: Extract makes no new WAV. Stock: a new WAV for each chop.

START-UP AND SAFETY

- At start-up, the MPC asks "Start SampQOL OS?". B1 = Start.
  B3 = Cancel = stock 1.3.0.
- A start-up screen shows the logo and the version. Ready in about 17 s.
- A watchdog starts the mod again after a crash. Two crashes = stock.
- On a firmware other than 1.3.0, the MPC starts as stock.
- Crash logs go to the SD card.
- A safety net keeps the samples that stock deletes at load. If stock
  changed the project, the MPC asks to load or delete the recovery copy.
- The browsers do not show the "autorun" folder of the mod.
- SD Card Access works with the mod.

SETTINGS

- A new Settings screen, next to MIDI Config, with all mod options.
- Start drive for samples and for projects, CPU/RAM monitor and battery
  monitor.
- Home Folder: Sample Select always opens in a folder that you choose.
- Show Knob Values, screen brightness and pad brightness.
- Shortcuts: pick the screen of SHIFT + pad, SHIFT + SAMPLE and PAD FX.
- Hide the triplet grids.
- Count-In at Start: Count-In is on when the MPC starts (default). Set it
  to Off to start without Count-In.

KNOBS

- All knobs on the mod screens follow Parameter Takeover, as stock knobs.

SAMPLE MODE

- Pad EQ: a 3-band kill EQ for each pad, with a live spectrum.
- Sample Tail: the end of a sample fades out, so a chop does not stop
  abruptly.
- 16 Levels Scale: the pads play a scale, a chord or an arpeggio.
- Keys: a MIDI keyboard plays the selected pad chromatically.
- A chord on one pad (Keys, 16 Levels Scale): each tone plays with its own
  length.
- The SAMPLE screen shows the sequence, the track and the pad.
- Chop Mode: up to 128 slices.
- Chop Mode: Chop Type Threshold and Split make up to 64 slices.
- Chop Mode: Chop Type Regions also gives 32 and 64 regions.
- Chop Mode: Link Slices for each pad, and a question before a rechop.
- Chop Mode: an option (Settings) to snap to a zero crossing at every zoom
  level.
- Chop Mode: Extract a chop to a pad, without a new WAV.
- Chop Mode: when you leave Chop Mode, a Threshold or Regions chop becomes
  Manual. The slices stay, also on an MPC without the mod.

SAMPLE RECORD

- To Seq: the recording goes in the sequence as a note.
- Looper: record more than one pass in one loop.
- Undo and Redo work on SAMPLE RECORD. A take is one undo step.
- STOP stays on SAMPLE RECORD.
- The Input Config settings are on SAMPLE RECORD.

SEQUENCES AND TRACKS

- Up to 16 drum tracks. Each track has its own 8 banks.
- Select a track, or make a new track.
- Copy a pad to another track.
- Select a sequence from any screen. It changes at the end of the loop.
- While you hold SEQ, the pads show the sequences (green, yellow, red), also
  on the Flex Beat and Pad FX screens.
- The SAMPLE screen shows the next sequence until the end of the loop.
- Select grid: a 4x4 grid shows the tracks or sequences while you select.
  A new track shows as "+".
- Rename a sequence, with ready-made names.
- SEQ mode: only sequences with events light up.
- Mutes: record pad mutes and track mutes.
- After Explode or Delete, a track named "Track n" gets the number of its
  new place. Names that you gave stay.

STEP EDIT

- Pad View: pads on the rows, 1 to 8 bars.
- Pad View: the selected pad lights up brighter on the pads.
- Track View: all drum tracks on the rows, 1 to 8 bars.
- Steps: the 16 pads are 16 steps, as on a step sequencer.
- Steps: 1 bar in 1/16 by default. Zoom from 1/64 to 16 bars without a
  change to your Time Correct.
- Pad View and Track View show the end of the sequence.
- Select notes, then move them, and set the length and the velocity.
- Copy, Paste, Duplicate and Clear, also into another sequence.
- Track View: Automate writes a volume or filter fade in the selection.
- Explode: each pad gets its own track.
- Track View: delete a track, with its notes in all sequences.
- View Follow: the Pad View and the Track View follow the playhead or the
  cursor.
- Play the selection in a loop.
- Finer grids: 1/64 and 1/64T.
- Pad View: zoom the view to 1, 2, 4 or 8 bars, as in the Track View.

MIXER AND EFFECTS

- PAD FX opens the Mixer, from every mode.
- Mixer: Pad, Track, Submix (1-8) and Master, with 4 inserts each.
- Ch. Mixer and Pad Mixer: 8 strips per page with meters, Mute and Solo.
  Turn the encoder for more strips.
- Ch. Mixer and Pad Mixer: K2 = pan. The FADER and the encoder set the
  volume of the selected strip.
- Ch. Mixer and Pad Mixer: SHIFT + B1 opens the inserts of the selected strip.
- FADER menu: Track Volume (stock: Kit Volume) sets the volume of the
  current track, as K1 on the Ch. Mixer. REC + PLAY records it.
- Rename a pad, track or submix, and give it a colour.
- Remove or hide the fixed master effects. A hidden effect keeps working
  and frees its slot. On another MPC it does not play.
- Undo works in the Mixer. REC + PLAY records on/off and volume.
- Knob FX is on the master: all pads play through it.
- Color Comp. on a track or submix.
- Pad FX: Fixed holds the effect after a tap. The FADER sets the depth.
- FX Select List: show only the effects that you use.
- Effect page: LP, HP and BP Filter show a filter curve with the cutoff and
  a live spectrum of the sound.
- Effect page: the Limiter shows meters for input, gain reduction and output.
- Effect page: on a track or a submix, the knobs of each effect record
  automation.
- Kill EQ as an insert on a track, a submix or the master: the 3-band kill
  EQ of the pad, with the same page and live spectrum. On a track or a
  submix, the knobs record automation.
- Mixer: select a pad, track or submix with PAD FX and the encoder.

UNDO AND REDO

- The Undo popup shows a counter and tells what changed.

ERASING

- ERASE AUTOMATION: erase all automation of a pad at once.
- ERASE + FADER (Track Volume): erase the volume automation of the current
  track in this sequence.

LOADING AND SAVING

- Sample Select: Preview with its own volume. The selected pad also
  previews the file.
- Sample Select: make a new folder on the SD card.
- Save Sample and Save Project: choose the folder. The MPC remembers the
  last folder.
- Save Project asks before it replaces a project with the same name.
- Browsers: jump 10 rows, and remember your place in each folder.
- Save Project: a random name.
- New Project from a template.
- Export: a mixdown and stems of a sequence (WAV 24-bit 44.1 kHz). The file
  names have the project name, and the tempo if you select it.
- Resample Pad: resample one pad with its tail, also with the Knob FX.

FIXED (STOCK PROBLEMS)

- 16 Levels: hold pad 1, push pad 2, release pad 1: pad 2 stopped too.

KNOWN ISSUES

- A stock MPC Sample loads only the first drum track of a SampQOL project.
  Recorded pad mutes and the Track Volume do not play there.
- Without SampQOL OS, the Pad EQ and a Hidden master effect do not play, and
  the Kill EQ insert plays as a PEQ 2-Band, 2-Shelf (no kill).
- Without SampQOL OS, a chord on one pad in Note On mode stops at the
  shortest note.
