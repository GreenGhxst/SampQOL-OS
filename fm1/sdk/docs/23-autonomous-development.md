# Autonomous development and emulator testing

The [virtual MPC Sample](https://github.com/orrpunk-art/virtual-mpc-sample) is a
full software emulator of the MPC Sample 1.3.0.12: the stock app runs under QEMU,
so a mod can be built, loaded, exercised and inspected without a physical unit.
That makes it useful both for a person and for an LLM coding agent: the agent
gets a real app, real events, real audio plumbing and a reproducible failure it
can read.

## What you need

- Docker, QEMU, OpenSSH and the emulator repo's firmware inputs (see the
  emulator README).
- The SDK's [`build-mod.sh`](22-build-and-install.md).
- A mod that builds as a Rust `cdylib`.

The emulator is the source of truth for *behavior that needs the app*: hooks,
menus, Extract, drawing, audio callbacks, MIDI and screen state. It is not a
substitute for the physical device's audio, timing or hardware quirks; treat a
device-only failure as a separate investigation.

## First start

```sh
git clone https://github.com/orrpunk-art/virtual-mpc-sample
cd virtual-mpc-sample
python3 lab.py build          # once; builds kernel, bridge and disk images
python3 lab.py run --headless # panel on http://127.0.0.1:8765/
```

`run` starts QEMU and the panel server. The app is ready when
`python3 lab.py status` shows the panel and SSH ports open and the Screen tab
shows the MPC Sample. `python3 lab.py ssh` gives a root shell in the guest.

## The fast loop

From the mod repository:

```sh
# 1. build through the SDK script
./mod/build.sh

# 2. load the resulting .so into the running app
python3 /path/to/virtual-mpc-sample/lab.py preload \
    ./mod/build/my-mod.so

# 3. look at the screen
curl -s http://127.0.0.1:8765/screen.png -o /tmp/my-mod.png

# 4. run a test
python3 mod/tests/emulator_menu.py
```

`preload` copies the library, installs the `/run` systemd drop-in, restarts the
app and checks that the library is mapped. It is the fastest edit/build/test
loop. A rebuild is required after code changes; `preload` alone does not reload
the old library.

## What a test can observe

The emulator exposes three useful surfaces:

- **Panel HTTP** — `POST /control` sends keys, pads, encoder turns and knob
  values; `GET /screen.png` returns the 320×240 screen; `/state` returns LED and
  control state.
- **SSH** — `lab.py ssh '<command>'` reads process memory, logs, files and systemd
  state. `lab.py ssh` is also how a test checks that the app did not restart.
- **The app itself** — a test can drive the real SAMPLE OPTIONS menu, render a
  sample, and then read the resulting WAV or the mod's exported status symbols.

The minimod tests are working examples:
[`mod/tests/`](https://github.com/ioma8/mpc-minimod/tree/main/mod/tests). They
show menu navigation, HPSS, effects, bass, spectral chop, MIDI, the looper and
AutoTrim.

## A small autonomous check

An agent should make each step produce a clear pass/fail and leave useful
artifacts. This is a minimal shape:

```sh
#!/bin/sh
set -eu

LAB=/path/to/virtual-mpc-sample
MOD=/path/to/my-mod

"$MOD/mod/build.sh"
python3 "$LAB/lab.py" preload "$MOD/mod/build/my-mod.so"

# Replace this with the test's own assertion.
python3 "$MOD/mod/tests/emulator_smoke.py" --lab "$LAB" \
    > /tmp/my-mod-test.log 2>&1 || {
        cp /tmp/my-mod-test.log /tmp/my-mod-test.failed.log
        curl -s http://127.0.0.1:8765/screen.png -o /tmp/my-mod-failed.png
        exit 1
    }

echo "PASS: /tmp/my-mod-test.log"
```

The test script itself should use the panel and SSH as above, assert on concrete
behavior (a status string, a pad, a WAV, a screen pixel, a process id), and
print one final `PASS`/`FAIL` line.

## An LLM agent loop

A reliable loop is deliberately boring:

1. **Orient** — read the task, the SDK page for the area, and the current test.
2. **Change one thing** — keep the diff small and feature-local.
3. **Run the host gate** — `cargo fmt --check`, clippy, unit tests.
4. **Build** — `./mod/build.sh`; do not skip the symbol/glibc checks.
5. **Preload** — `lab.py preload ...`; confirm `mapped in MPC Sample`.
6. **Exercise the real app** — run the focused emulator test, capture the
   screenshot and log.
7. **Diagnose from evidence** — status, screen, log, memory and process id, not
   guesses.
8. **Repeat** until the focused test passes, then run the full suite once.
9. **Clean up** — stop the lab or leave it in a known state; remove temporary
   files.

Good agent prompts include the exact build command, the test command, the
artifact directory, a time limit and the expected final evidence. Avoid “make it
work” with no observable check.

## Guardrails

- **Pin the emulator checkout.** Record its commit beside the firmware version.
- **Check the firmware.** The SDK refuses the wrong image; the autorun package
  also checks the app's first 4 KiB. Do not disable either check.
- **Bound every loop.** Use a timeout and a maximum retry count; an emulator
  boot can take tens of seconds.
- **Reset deliberately.** `lab.py reset` discards guest `/data` changes; use it
  for a clean fixture, not in the middle of a test that needs state.
- **Do not assume the emulator is hardware.** It verifies logic and UI; audio
  latency, USB and hardware timing still need the real device.
- **Stop the lab.** `python3 lab.py stop` shuts the guest down cleanly. Do not
  leave a headless QEMU and a panel server running after the loop.

## Links

- Emulator: **https://github.com/orrpunk-art/virtual-mpc-sample**
- Emulator usage and ports: see the emulator repo's `docs/USAGE.md`
- minimod test examples: **https://github.com/ioma8/mpc-minimod/tree/main/mod/tests**
- Build and install: [Build and install a mod](22-build-and-install.md)
