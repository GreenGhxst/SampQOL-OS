# SPDX-License-Identifier: GPL-3.0-only
"""Judge the audio recorded by test_app: silence where no note plays, sound
where one plays, the app's own signal untouched, no clipping."""
import struct, sys, os, math

def load(path, fmt):
    data = open(path, 'rb').read()
    if fmt == 'i32':
        return struct.unpack('<%di' % (len(data) // 4), data), 2 ** 31
    return struct.unpack('<%dh' % (len(data) // 2), data), 2 ** 15

def rms(samples, a, b):
    seg = samples[a * 2:b * 2]
    return math.sqrt(sum(x * x for x in seg) / max(1, len(seg)))

def check(out_dir, raw, fmt, plan_lines):
    samples, full = load(os.path.join(out_dir, raw), fmt)
    ok = True
    peak = max(abs(x) for x in samples) if samples else 0
    print(f"{raw}: {len(samples)//2} frames, peak {peak/full:.3f} of full scale")
    if peak >= full - 1:
        print("  FAIL: clipping / wrap-around"); ok = False
    for line in plan_lines:
        parts = line.split()
        kind, a, b = parts[0], int(parts[1]), int(parts[2])
        r = rms(samples, a, b) / full
        if kind == 'silence':
            good = r < 1e-6
        elif kind == 'loud':
            good = r > 0.01
        elif kind == 'quiet':
            good = r < 0.05
        elif kind == 'dc':
            dc = int(parts[3]); seg = samples[a * 2:b * 2]
            good = all(x == dc for x in seg); r = dc / full
        else:
            good = False
        print(f"  {kind:8s} frames {a:7d}-{b:7d}: rms {r:.5f} {'OK' if good else 'FAIL'}")
        ok = ok and good
    return ok

out_dir = sys.argv[1]
plan = open(os.path.join(out_dir, 'plan.txt')).read().splitlines()
streams = []
for line in plan:
    if line.startswith('stream '):
        _, raw, fmt = line.split()
        streams.append((raw, fmt, []))
    elif streams:
        streams[-1][2].append(line)
ok = True
for raw, fmt, lines in streams:
    ok = check(out_dir, raw, fmt, lines) and ok
print("check.py:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
