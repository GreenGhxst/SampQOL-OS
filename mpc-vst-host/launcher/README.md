# launcher/ (reserved; nothing shipped yet)

This folder will hold the SD card launcher that chains the expansion's
runtime adapter into SampQOL's boot (`autorun.sh`, `autorun.json`, the
`autorun.check` generator and a static `setarch` stand-in that appends the
adapter library to `LD_PRELOAD` and applies the crash guard). The mechanism
is documented in `docs/AUDIT.md` (MPC-FM section) and the gate it would call
is implemented in `src/sampqol_compat/`.

It is intentionally empty in this release:

- There is no verified audio or MIDI integration to load (see
  `docs/SAMPQOL_INTEGRATION.md`), so a launcher would add a boot-time
  failure mode and deliver nothing.
- A launcher must be tested on the device before it is shipped; the
  specification forbids presenting an emulated run as compatibility.

When it is written, it will be this project's own code (MPC-FM's launcher
has no licence and is not copied) and will keep SampQOL's official
`autorun` folder unchanged beside it, exactly as the audit describes.
