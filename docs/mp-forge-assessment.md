# MP Forge, assessed for SampQOL-OS

Audit of [Forge-Audio/mp-forge](https://github.com/Forge-Audio/mp-forge) at version 0.1.5
(commit 9bcdbf1, 46 commits), carried out 2026-11-20, and what was taken from it.

## What it is

MP Forge is a free desktop app (Tauri 2, a Rust engine of about 39,000 lines of Rust and
JavaScript) in which a musician describes a plugin in plain words and an AI builds it with
Akai's MPC Plugin Builder SDK. It plays the plugin on the computer, previews the MPC screen,
runs twelve safety checks, and sends the result over the network to a USB drive or SD card in
an MPC. It is GPL-3.0-or-later. It is not affiliated with Akai or inMusic.

It does not replace the SDK. It vendors the very same repository this project carries as a
submodule, pinned in `sdk.pin` to commit `febd2ca`, nine commits behind the submodule here.
The newer SDK commits add per-page background images, Q-Link rectangle fitting, the
"no generic sliders" guidance and the "ask before installing" rule, all of which MP Forge
reimplements in its own layer.

## How it is built

| Part | What it does |
| --- | --- |
| `directives/` | The prompts given to the AI: maker, sound design, screen design, final message, plugin architect (asks questions first), lucky scout (surprise plugin), guide writer. About 770 lines of carefully written plain English. |
| `engine/src/gate.rs`, `source_scan.rs` | The safety gate: a source scan for forbidden calls and headers (processes, network, threads, file I/O, `exit`, `dlopen`, inline asm, token pasting), then build, validator, picture rendering, and seven host-simulator sessions (everyday, full chord, playing song, 32 and 512 frame buffers, 48 kHz, 30 seconds). |
| `engine/src/mcp_server.rs`, `claude_code.rs` | Drives the user's own Claude Code as the builder, locking off its shell and file tools and serving its own tool set (`check_plugin`, `look_at_screen`, `style_screen`, `edit_plugin_file`) over MCP. Also speaks the OpenAI-style chat protocol to OpenAI, Google, xAI, Mistral, DeepSeek, OpenRouter and Ollama. |
| `engine/src/pictures.rs`, `film_strip.rs`, `look.rs` | Its own layer above `ui.json`: SVG drawings with per-frame expressions rendered to film strips, a `look.json` for hand styling, and a hidden `Picture Clock` parameter so a plugin can animate its own screen. |
| `screen-tests/` | Ten probe plugins and a checklist of what an MPC screen really supports, measured on an MPC Key 37 at OS 3.10.1. |
| `app/src/key_store.rs` | API keys go into the OS keychain only; never to disk, never to the AI. |

Outbound hosts in the code are the AI providers, the Open MPC catalog on GitHub Pages and
raw.githubusercontent.com. Nothing about the user or their plugins is sent to the catalog.

## Assessment

**Strengths**

- The planning and design directives are the best writing on MPC plugin design found in
  either repository. They give the AI a point of view about sound and screen where the SDK's
  skills give it mechanics.
- The safety gate is thoughtful and honest about its limits ("a tripwire, not a wall").
  Seven simulator sessions with varied buffer sizes and sample rates catch more than the
  SDK's single `host_sim test` run.
- The screen-tests checklist is primary evidence about the hardware that the SDK docs do not
  have: images are never scaled, fixed text only renders when tied to a parameter, 30 frames
  per second is the cap, a plugin can keep a thread and reach the network, a shipped font
  falls back to Titillium Web.
- Key handling, the "send only to removable media" rule and the ask-before-install rule are
  the right defaults.

**Weaknesses and risks**

- It is a 0.1.x app, hand-tested on macOS and Windows only, unsigned installers, Linux
  untested by hand.
- Its Claude Code route depends on Claude Code honouring its own `--tools`, `--restricted` and `--allowedTools` switches
  and needs version 2.1.259 or newer; the lock-down is cooperative, not enforced.
- Its SDK pin lags; features in the newer SDK (page backgrounds) are duplicated by MP Forge's
  own mechanisms (`pictures.json`, `look.json`) that only exist inside MP Forge. A plugin
  designed in MP Forge is not fully portable back to a plain SDK checkout.
- Licence: GPL-3. Its prose and code cannot be copied into SampQOL-OS, whose licence is
  all-rights-reserved. Ideas and method can.
- The source scan bans network and threads, but its own checklist proves the MPC allows both.
  That is the right policy for a tool that ships plugins to other people's hardware, and it
  is also why nothing in MP Forge should be read as a sandbox.

## Combine or stay separate?

Do not combine the apps. MP Forge is a desktop GUI with its own engine; this repository is a
documentation site plus an SDK submodule driven by Claude Code skills. The useful overlap is
the method, not the code.

What was taken, rewritten in our own words:

- A new skill, `.claude/skills/mpc-plugin-plan/SKILL.md`, that sits in front of
  `mpc-plugin-new`. It asks the architect's three-to-five questions with vivid choices, then
  writes `plugins/<Name>/DESIGN.md` with `The sound` (signal flow, controls, movement,
  cleanliness, a `Default` preset first) and `The look` (subject, palette, kit, pages,
  blocks of four, backdrop). The other skills read that file before they act.
- The design rules were reconciled with the SDK: only `ui.json` features the SDK generator
  really has (`kit` plus `accent`, `style`, `pages[].background`, `text`, `qlinkGroups`)
  or a hand-written skin. MP Forge-only tools (`style_screen`, `pictures.json`,
  `look.json`, `Picture Clock`) were left out.

What was not taken, and could be later:

- The seven-session simulator matrix. The SDK's `host_sim` already takes `--seconds`, `--block`,
  `--sr`, `--notes` and `--playing`; `mpc-plugin-test` could be extended to run the same matrix.
- The source scan's forbidden-call list, as a `grep` pass before building.
- The screen-tests checklist, as a reference page for `mpc-plugin-ui` part B.

## Keeping this current

Re-check `sdk.pin` in MP Forge against the submodule here when either moves. Re-read its
`directives/` when it tags a new version; the method in `mpc-plugin-plan` should follow
their findings, in our words.
