#!/usr/bin/env python3
"""Fail-closed, privacy-minimal contract for diagnostic JSON."""
import json
import subprocess
import sys

binary = sys.argv[1]
result = subprocess.run([binary, "--evidence"], capture_output=True, text=True, timeout=10)
assert result.returncode == 0, result.stderr
report = json.loads(result.stdout)
assert report["kind"] == "diskmode-hardware-evidence-v1"
for key in ("hardware_verified", "ownership_granted", "recovery_verified"):
    assert report[key] is False, key
assert report["display"]["panel_verified"] is False
assert report["input"]["mapping_verified"] is False
assert "release" not in report["kernel"]
assert "serial" not in result.stdout.lower()
assert report["input"]["control_surface_hint"] in (True, False, None)
if not report["input"]["alsa_cards_readable"]:
    assert report["input"]["control_surface_hint"] is None
for volume in report["storage"]:
    assert volume["ownership_verified"] is False
    assert volume["ownership_observation"] in ("usb-export-detected", "unmounted", "unknown")
    assert "device" not in volume and "mount_point" not in volume
    if not volume["mounted"]:
        assert volume["fs_read_only"] is None and volume["filesystem"] is None
bad = subprocess.run([binary, "--evidence", "--activate"], capture_output=True, timeout=10)
assert bad.returncode == 64 and not bad.stdout
print("PASS: read-only evidence contract, uncertainty, privacy and no activation flags")
