---
name: mpc-plugin-plan
description: Plan an MPC plugin before any code is written. Turn a musician's idea into a settled brief and a DESIGN.md that fixes the sound (signal flow, controls, movement, presets) and the look (subject, palette, kit, pages) so that mpc-plugin-new, mpc-plugin-dsp and mpc-plugin-ui build one coherent plugin instead of a generic one. Use when someone describes an instrument or effect they want, asks for ideas or options, wants a plugin to feel like "its own thing", or before starting mpc-plugin-new on anything bigger than a utility.
---

> **Installed in SampQOL-OS.** This skill is the planning step in front of the Akai SDK skills. The SDK is a git submodule at `mpc-plugin-builder-sdk/`; every `tools/`, `docs/`, `framework/` and `plugins/` path below is relative to it. The method is adapted, in our own words, from the planning and design directives of MP Forge (Forge-Audio/mp-forge, GPL-3). Nothing here replaces the hard rules in `mpc-plugin-builder-sdk/AGENTS.md`; they always win.

# Plan an MPC plugin

The person you help is a musician, usually not a programmer. The SDK skills can build a correct plugin from a one-line description, but a correct plugin is often a forgettable one: one oscillator into one filter, a delay with a Mix knob, stock knobs in rows on a dark page. This skill exists to settle the idea first, so the build has a point of view.

Output of this skill: a short exchange with the user, then `plugins/<Name>/DESIGN.md` with two sections, `The sound` and `The look`, plus the manufacturer and plugin name, ready to hand to `mpc-plugin-new`.

## 1. Read the idea, then ask (once)

Settle silently, from the description, what the SDK needs anyway: effect or instrument (an arpeggiator or sequencer must be an instrument that makes its own sound; effects get no MIDI), and whether the manufacturer name is already given. If it is not, `mpc-plugin-new` will ask for it; fold that question into yours so the user is asked only once.

Then ask three to five questions, fewer if the idea is already detailed. Pick the questions whose answers would most change what gets built, each on different ground:

- the character of the sound (clean or worn, warm or cold, polite or feral)
- the one thing that makes it stand out
- what the hands do while it plays (the two or three knobs that matter)
- what moves by itself (wobble, drift, pumping, something that unfolds while a note is held)
- how it behaves over time or answers playing (velocity, keyboard position, input level, tempo)
- how its screen should look and feel

At least one question should open a door the user has probably not considered: an unusual sound source, a twist in how it reacts, a control that does something surprising.

Give each question three or four choices in vivid, plain words a musician would say ("worn cassette, slightly seasick", not "wow and flutter modulation"). The choices of one question should be truly different from each other. Mark a question as multi-select only when its choices genuinely combine. Use `AskUserQuestion` when it is available, otherwise a numbered list.

Never ask about things that are settled: the name once given, who made it, effect versus instrument. Never ask twice about the same thing, and never ask what you could settle yourself from the SDK docs. If the user says "just build it", skip to step 2 with sensible choices and say in one line which ones you made.

Working within what the MPC can do:

- An effect changes sound played through it and hears no notes. An instrument makes sound from notes.
- A plugin cannot send notes to the MPC, control other plugins, use the network or start threads.
- The MPC is a slow ARM device: no long convolution reverbs, no huge voice counts, no machine learning.
- Its screen is pages of up to sixteen controls, each page mapped to the sixteen hardware knobs in groups of four.
- It can play sound files shipped in the plugin's `Content/` folder and ship a handful of presets.

## 2. Write `DESIGN.md`

Create `plugins/<Name>/DESIGN.md` (make the folder if `new_plugin.py` has not run yet; the scaffold will not overwrite it). Two sections, each short enough to read in a minute. Every later skill reads this file first and keeps it current when the sound or the look changes.

### The sound

- **One sentence** that says what it sounds like and what sets it apart. "A delay whose repeats sink and smear like a tape left in the sun", not "a delay with modulation".
- **Signal flow**, one line per stage, in order.
- **Controls**: for each, its on-screen name (12 characters or fewer, a musician's word), kind (float, int, bool, choice), range with unit, default, and what it does to the sound across its whole travel. A control with a dead half is a control to redesign: use a log range for frequencies and times, a skew where the detail sits near zero. If two knobs must always be turned together, make them one.
- **Big controls first**: one or two macros that move several things at once (brighter plus tighter plus louder) are worth more than three small knobs the musician has to balance. They lead the `MAIN` page.
- **Movement**: what moves by itself and how much. Size it to the plugin:
  - small or "keep it simple": one or two sources wired to where they sound best, each with rate and depth named for what is heard (`Wobble`, `Drift`, `Pump`), no routing choices;
  - fuller effect or simple synth: one or two sources with their own controls, an amount, and a `To` choice of three to five destinations;
  - multi-page synth or an explicit request for a mod matrix: a `MOD` page with up to four routes (Source, To, Amount), at most eight sources and eight destinations.
  Amounts run from minus to plus and rest at zero. Movement adds to where the knob stands, in the knob's own units (octaves or semitones for pitch, dB for level), clamped and smoothed before it reaches a filter or delay. Never route movement to a switch, a choice or the master volume.
- **How it answers playing**: for an instrument, how velocity, keyboard position, pitch bend, mod wheel and pressure shape the tone, not only the loudness. For an effect, how input level and song tempo can drive it (`framework/dsp/tempo.h`, `framework/dsp/lfo.h` sync).
- **Cleanliness plan**: name what keeps each control from clicking, zipping or blowing up at its worst setting combined with every other control at its worst (full resonance with full drive with full feedback). Smoothers on everything audible, minimum attack about 1 ms and release about 5 ms, feedback under 1 with a soft clip in the loop, a DC blocker after asymmetric shaping, output peaking between -12 and -6 dB at defaults and never past 0 dB. Dirt is allowed only when it is the idea, and then it is written down here.
- **Presets**: program 0 is `Default` and holds exactly the starting value of every control, so the name the MPC shows on load agrees with the sound. Then three to eight more, each a different use rather than a variation, named for what is heard, roughly equal in loudness, together using every kind of movement the plugin has. One of them has no movement. (`mpc-plugin-presets` has the `addProgram` mechanics.)

### The look

- **The subject, in one sentence**: the object or place the plugin is, taken from the same idea as the sound. A worn tape machine, a sonar room, a brass laboratory instrument, a rain-streaked window. Colours, materials, shapes and words come from that world.
- **Palette**: four to six colours, each with a name, a hex value and a job: the ground, the panels, one loud accent used sparingly, a quieter second accent, the lettering. Light screens are allowed; dark is Akai's habit, not a rule.
- **Kit**: one of `mpc-red`, `amber`, `cyan`, `lime`, `violet`, `mono`, chosen last to fit the palette, with an `accent` colour when the palette wants one. One kit for the whole plugin.
- **Pages**: `MAIN` first, its top row the four controls touched first (knobs or faders, never switches). Then section pages named in uppercase, eight characters or fewer, often two words with a slash (`FILTER/ENV`). Rarely used settings on a last page called `MORE`. Effects usually one or two pages, synths four to six.
- **Blocks of four**: the plugin's single `qlinkGroups` orientation (columns by default), and for every page which four controls form each group. A group is one coherent set (an envelope's four stages; cutoff, resonance, drive, tracking), never a set split across two groups. Complete a block with `empty` or a `text` heading rather than a stray control.
- **Widgets**: bool or 2 to 4 choices as `switch`; levels, mixes, sends and envelope stages as `fader`; anything centred on zero as a bipolar `knob`; everything else a `knob`; long choice lists a `knob` with `"overlay": "menu"`. Never the generic `slider`. Same role, same widget on every page.
- **Backdrop**: whether each page gets a background PNG (`pages[].background`, 1280 wide, from the project's `assets/` folder) and what it shows: section panels with uppercase title strips, a name plate, scales and bezels around the places the dials will sit, depth from one light direction, texture in the margins and quiet behind lettering. One flat colour is the default, not a design. Captions drawn into the backdrop are the way to use a font the MPC does not have; live values stay in the MPC's own lettering (Titillium Web, Roboto or Nexa).
- **The one bold thing**: a single strong idea carried through, kept calm everywhere else, so the screen can be told from the musician's other plugins across the room.
- **Signs of a default** to check against before calling the look planned: untouched grid, one flat colour, every control the same size and the kit's own lettering everywhere, the kit's colour the only colour, a page that could be swapped with another plugin's unnoticed.

Keep it honest about what the SDK's `ui.json` generator can and cannot do. It places controls on the 4 x 4 grid and draws kit film-strips; it does not resize dials, letter names in other fonts or animate pictures. Those need a hand-written skin (`mpc-plugin-ui`, part B) or a backdrop image, and the plan should say which.

## 3. Hand over

End with a short recap to the user: the plugin and manufacturer names, the sound in one sentence, the look in one sentence, the pages and their groups, and the presets. Then continue with `mpc-plugin-new`, which scaffolds and builds from `DESIGN.md`. When the user asked for options or ideas, change nothing until they have picked.

While building, the other skills hold to the plan:

- `mpc-plugin-dsp` writes what `The sound` says and reads `DESIGN.md` before every later change.
- `mpc-plugin-ui` lays out what `The look` says.
- `mpc-plugin-test`: a WARN about clipping, silence or CPU is a fault in the sound, not a note. Fix it or say in one sentence why not.
- You cannot hear the plugin. Reason each control through at low, middle and high with the others at their worst, and tell the user what it is meant to sound like, not that it sounds good.

## What to say when it is done

Three plain sentences, no more: what it is, the one thing worth trying first, and anything asked for that was left out or done another way. Everything else belongs in `README.md`.
