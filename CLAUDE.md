# CLAUDE.md

SampQOL OS is a free mod for the Akai MPC Sample, published as a GitHub Pages
site. `index.html` is the manual, `README.md` the overview and `CHANGELOG.md`
the release history. Keep those three in agreement when you change features.

## MPC plugin development

This repo carries the **MPC Plugin Builder SDK** (inmusic-software) as a git
submodule at `mpc-plugin-builder-sdk/`. It builds VST2 instrument and effect
plugins for Akai MPC standalone hardware. After a fresh clone run:

    git submodule update --init

Before any plugin work, read `mpc-plugin-builder-sdk/AGENTS.md`: it holds the
workflow (scaffold, code, UI, build, test, package) and the hard rules
(realtime safety, permanent identity, matching names, user-controlled
installs, no system modification).

Then use the skills in `.claude/skills/`. Eight mirror the SDK's own; `mpc-plugin-plan` is ours, a planning step adapted from MP Forge's method (see `docs/mp-forge-assessment.md`):

| Skill | When |
|---|---|
| `mpc-plugin-plan` | Settle the idea first: questions, then `DESIGN.md` (sound and look). Start here for anything bigger than a utility |
| `mpc-plugin-new` | Start a plugin from a description |
| `mpc-plugin-dsp` | Audio code, parameters, MIDI, state |
| `mpc-plugin-ui` | Pages, knobs, layout, custom skins |
| `mpc-plugin-build` | Compile native + arm32 + arm64, fix build errors |
| `mpc-plugin-test` | Host simulator, WAV renders |
| `mpc-plugin-package` | Zip with install instructions |
| `mpc-plugin-deploy` | Install on an MPC over the network (only when asked) |
| `mpc-plugin-presets` | Built-in programs |
| `mpc-plugin-troubleshoot` | Problems on the MPC or with the tools |

Every path the skills name (`tools/`, `docs/`, `framework/`, `plugins/`,
`ui-kit/`, `schemas/`, `examples/`) is relative to the submodule, so `cd
mpc-plugin-builder-sdk` before running their commands. New plugin projects
go in `mpc-plugin-builder-sdk/plugins/<Name>/`; the submodule's `.gitignore`
keeps them out of the SDK's own history, so commit them from this repo only
if they are meant to ship with SampQOL OS.

A plugin's `DESIGN.md` is the source of truth for its sound and look. Read it before
changing a plugin, and update it when the sound or the look changes.
