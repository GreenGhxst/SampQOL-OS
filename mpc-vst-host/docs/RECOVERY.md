# Recovery

## Today (standalone host only)

The expansion is a folder of files and command line tools. It is never
started by the MPC at boot, so there is no boot to recover:

- The MPC hangs or misbehaves: hold the power button until it is off,
  remove the card, start without it. That is the stock MPC. Then, on a
  computer, delete `VST-Expansion` from the card if you want it gone.
- SampQOL does not start: the expansion did not touch its `autorun` folder
  or `mpcqol.conf`; follow the SampQOL manual's troubleshooting.
- A plug-in hangs or crashes the scanner: it was run in the worker process
  with a 10 s timeout and a 512 MB limit; the host records it in
  `Cache/registry.txt` as `quarantined` with the signal or timeout and will
  not probe it again until the file changes or the line is deleted.
- `mpcvst render` itself crashes: the plug-in ran in-process (that is what
  "experimental, in-process" means in `STATUS.md`). Nothing else is
  affected; delete or move the plug-in.
- A state file will not load: it is checked with a CRC and refused when
  damaged. Nothing is deleted; `mpcvst render --state FILE` reports the
  reason. Keep the `.mvxs` file; the plug-in it belongs to is named in it.

## When a runtime adapter exists (design, implemented as logic in `sampqol_compat`)

- `VST-Expansion/Config/DISABLE`: the adapter never installs anything.
- Crash guard: at most 3 starts per boot with the adapter; if the app stops
  less than 600 s after a start with the adapter, it stays off until the next
  boot. SampQOL keeps starting normally in both cases.
- Last-known-good: the registry and state files are plain files; a
  `Cache/registry.txt` from a good run can be copied back.
- Safe mode (`safe_mode = on` in `host.conf`): every plug-in starts
  bypassed.
- Remove the card: the MPC is stock.
