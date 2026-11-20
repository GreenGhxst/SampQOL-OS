#!/bin/sh
# Starts the Web Console against a simulated storage tree so the frontend and
# API can be exercised on a development machine. Nothing here touches real
# devices. Usage: tools/run-sim.sh [build-dir]   (then open the printed URL)
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-$HERE/build}; [ $# -gt 0 ] && shift
SIM=${DM_SIM_ROOT:-${TMPDIR:-/tmp}/diskmode-sim}
mkdir -p "$SIM/media/3265-3932/Samples/User" "$SIM/media/3265-3932/Samples/User/Export" "$SIM/media/3265-3932/Projects" \
         "$SIM/media/3265-3932/autorun/logs" "$SIM/data/UserContent/Samples" "$SIM/data/UserContent/Projects/User" "$SIM/content/Samples" "$SIM/appdata"
[ -f "$SIM/media/3265-3932/Samples/User/kick.wav" ] || python3 -I - "$SIM" <<'PY'
import os, struct, sys, random
root = sys.argv[1]
def wav(path, frames, freq):
    data = bytearray()
    for i in range(frames):
        v = int(12000 * (1 - i / frames) * (1 if (i * freq // 44100) % 2 == 0 else -1))
        data += struct.pack("<h", v)
    hdr = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE" + b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, 44100, 88200, 2, 16) + b"data" + struct.pack("<I", len(data))
    with open(path, "wb") as f: f.write(hdr + data)
sd = os.path.join(root, "media/3265-3932")
for n, fr, fq in [("kick", 22050, 60), ("snare", 15000, 180), ("hat", 6000, 2000), ("clap", 9000, 900), ("kick copy", 22050, 60)]:
    wav(os.path.join(sd, "Samples/User", n + ".wav"), fr, fq)
wav(os.path.join(root, "data/UserContent/Samples/factory kick.wav"), 20000, 55)
wav(os.path.join(root, "content/Samples/demo loop.wav"), 44100, 110)
open(os.path.join(sd, "Projects/My Beat.xpj"), "w").write("{}")
open(os.path.join(sd, "autorun/autorun.sh"), "w").write("#!/bin/sh\n")
open(os.path.join(sd, "mpcqol.conf"), "w").write("# settings\n")
PY
cat > "$SIM/volumes.json" <<JSON
[
 {"id":"sdcard","name":"microSD card","kind":"removable","mount_point":"$SIM/media/3265-3932","directory":true,"writable":true,"format_candidate":true,"protected_paths":["autorun","mpcqol.conf",".diskmode"]},
 {"id":"internal-samples","name":"Internal: Samples","kind":"internal","mount_point":"$SIM/data","directory":true,"subdir":"UserContent/Samples","writable":false},
 {"id":"internal-projects","name":"Internal: Projects","kind":"internal","mount_point":"$SIM/data","directory":true,"subdir":"UserContent/Projects/User","writable":false},
 {"id":"factory-content","name":"Factory content","kind":"internal","mount_point":"$SIM/content","directory":true,"writable":false},
 {"id":"usb-storage","name":"USB storage (unsupported)","kind":"usb","mount_point":"/nonexistent","directory":true}
]
JSON
echo "simulated storage: $SIM"
exec "$BUILD/diskmode-webconsole" --data-dir "$SIM/appdata" --web-root "$HERE/web" --volumes-json "$SIM/volumes.json" \
     --bind 127.0.0.1 --port "${DM_PORT:-8765}" --pairing-code "${DM_PAIRING_CODE:-DISKMODE}" --screen-source simulated "$@"
