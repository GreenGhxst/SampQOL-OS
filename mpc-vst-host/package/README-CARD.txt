MPC Sample VST Host Expansion (mpc-vst-host)
============================================

What is on this card folder
---------------------------
VST-Expansion/
  Host/            mpcvst (command line host), mpcvst-probe (scan worker),
                   run-selftest.sh, VERSION
  Plugins/         put your own aarch64 Linux VST2 plug-ins (.so) here:
    Instruments/   synths
    Effects/       effects
  Test-Plugins/    this project's mock instrument and effect, for the self-test
  Presets/ State/  plug-in presets and sidecar state files (one per pad/track)
  Cache/           the plug-in registry (delete it to force a full rescan)
  Logs/            scan and self-test logs
  Config/host.conf settings. A file Config/DISABLE turns everything off.

STATUS OF THIS RELEASE
----------------------
This is the STANDALONE host: it loads, validates, scans and renders VST2
plug-ins offline, from the command line (over SSH, or from a script).
It does NOT yet play plug-ins from the pads inside SampQOL OS: no verified
way to route plug-in audio and pad MIDI through the stock sampler exists
yet, so the SampQOL adapter is present but locked closed. SampQOL OS and
the stock app are not touched by anything in this folder.

The binaries were cross-compiled for aarch64 and tested under QEMU user
emulation on a PC. They have NOT been run on an MPC Sample yet. Run the
self-test first (see below) and read Logs/selftest.log before trusting them.

Self-test on the device (needs root SSH, see the SampQOL manual / mpc-fm notes)
------------------------------------------------------------------------------
The card is mounted noexec, so the self-test copies the host to RAM first:
    sh /media/<card>/VST-Expansion/Host/run-selftest.sh
It scans Test-Plugins/, renders a chord with the mock instrument through
the mock effect to Logs/selftest.wav and writes timings to Logs/selftest.log.

Removal
-------
Delete the VST-Expansion folder. Nothing else on the card or the MPC is
changed by it. Your SampQOL "autorun" folder is not part of this expansion.
