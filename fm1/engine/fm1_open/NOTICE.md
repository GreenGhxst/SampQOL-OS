# Vendored files from m-vave/fm1_open

Source: https://github.com/m-vave/fm1_open
Commit: 4c7496daac04a95ff10d1afdcc8f9ead2dbabc41 (2026-10-21, "Fix Quick Edit display and support JieLi 2.5.3")

Only the parts needed for the open DX7 engine are copied here, unchanged:

| Path | License (SPDX header in each file) |
|------|------------------------------------|
| `web/synth/dx7core.c`, `dx7ctrl.h` | Apache-2.0 (Google MSFA, modified by M-VAVE) |
| `web/synth/dx7*.c`, `dx7*.h`, `adsr.*`, `fixed.c`, `dx7synth.*`, `synth_dx7.h`, `types.h` | GPL-3.0-only WITH GPL-3.0-linking-exception |
| `third_party/synth/engine.h`, `fixed.h` | MIT |
| `sdk/include/fm1_block.h` | GPL-3.0-only WITH GPL-3.0-linking-exception |
| `app/fm_factory.inc` | Apache-2.0 (M-VAVE original V15 factory DX7 bank) |

`LICENSE.md` and `THIRD_PARTY.md` are the upstream notices. The proprietary
FM1 engine binary (`sdk/lib/libfm1_engine.a`) and the FM1 firmware are NOT
included and are not used: everything here builds from C source.
