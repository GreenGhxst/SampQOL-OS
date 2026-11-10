# Patching a SampQOL OS autorun folder by hand

`apply.sh` knows the scripts of SampQOL OS 0.2.0. For another version, make
these three changes yourself (then check the pin block as shown below).

1. In `autorun/qol-run.sh`, right after the line that copies `mpcqol.so`
   to `/tmp` (`cp "$DIR/mpcqol.so" "$SO" 2>/dev/null`), add:

   ```sh
   # fm1mix: open FM1 DX7 synth mixed into the app (see fm1/README.md)
   FM1SO=
   if [ -f "$DIR/fm1/fm1mix.so" ]; then
       cp "$DIR/fm1/fm1mix.so" /tmp/fm1mix.so 2>/dev/null && chmod 644 /tmp/fm1mix.so && FM1SO=/tmp/fm1mix.so
       export FM1MIX_DIR="$DIR/fm1"
   fi
   ```

2. In the same file, change `export LD_PRELOAD="$SO"` to

   ```sh
   export LD_PRELOAD="$SO${FM1SO:+:$FM1SO}"
   ```

   Optional, after `CODE=$?`: `[ -n "$FM1SO" ] && cp /tmp/fm1mix.log "$DIR/fm1/fm1mix.log" 2>/dev/null`

3. `autorun/autorun.sh` holds a list `PINS="..."` of SHA-256 checksums and
   refuses to start the mod when a file differs. Replace the checksum on the
   `qol-run.sh` line with the new one: `sha256sum autorun/qol-run.sh`.

4. Create `autorun/fm1/` with `fm1mix.so` (from `make`) and `fm1.conf`
   (from `card/fm1.conf`).

Check: in the `autorun` folder, run the pin test as `autorun.sh` does:

```sh
PINS=$(awk '/^PINS="/{sub(/^PINS="/,""); p=1} p{ if (sub(/"$/,"")) { print; exit } print }' autorun.sh)
echo "$PINS" | sha256sum -c
```

All four lines must say OK. `sampqol-0.2.0-fm1.patch` is the reference
result for 0.2.0 (`patch -p0 < sampqol-0.2.0-fm1.patch` inside the folder
that contains `autorun`).
