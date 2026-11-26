#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 R. Luke DuBois and Brad Garton
"""docs/synthaccess/Looching2026.json (the MIDItoSpeech sidecar) against the firmware: the file is the one
tools/gen_synthaccess.c makes now; every control the FM-1 sends has an entry; for every value it sends at every panel
step, MIDItoSpeech's reading (its common/parser.js modes, done here the same way) says exactly what the screen shows:
the names word for word, the percentages as whole numbers, the limits with the screen's decimals.
  tests/synthaccess_check.py build/host/gen_synthaccess docs/synthaccess/Looching2026.json"""
import json
import math
import subprocess
import sys

gen, path = sys.argv[1], sys.argv[2]
fresh = subprocess.run([gen], capture_output=True, text=True, check=True).stdout
committed = open(path).read()
failed = 0


def check(what, ok):
    global failed
    print(f"{what:<62} {'ok' if ok else 'FAIL'}")
    failed += not ok


def js_round(x):                                   # Math.round
    return math.floor(x + 0.5)


def read(entry, v):                                # parser.js parseSpeak, for the modes the sidecar uses
    d = entry["data"]
    if d == "percent":                             # mtopct
        return str(js_round(v / 127 * 100))
    if d == "floatmap":                            # vmap (linear), then toFixed(precision)
        a, b, c, e = entry["map"][:4]
        if entry.get("clamp") == "true":
            v = min(max(v, a), b)
        return f"{c + (v - a) / (b - a) * (e - c):.{entry.get('precision', 2)}f}"
    if d == "enumsplit":
        idx = 0
        for i, s in enumerate(entry["split"]):
            if v < s:
                break
            idx = i
        return entry["enum"][idx]
    if d == "offon":
        return "off" if v == 0 else "on"
    raise ValueError(d)


check("the sidecar is current (as gen_synthaccess makes it now)", fresh == committed)
dev = json.loads(committed)["device"]
bad, n, counts = [], 0, {}
for line in subprocess.run([gen, "--pairs"], capture_output=True, text=True, check=True).stdout.splitlines():
    kind, key, sent, mode, want = line.split(" ", 4)
    n += 1
    counts[kind] = counts.get(kind, 0) + 1
    if kind == "PC":                               # program_change: enum "presets" (value = index)
        got = dev["presets"][int(sent)] if int(sent) < len(dev["presets"]) else sent
    else:
        entry = dev["CC" if kind == "CC" else "NRPN"].get(key)
        if entry is None:
            bad.append(f"{kind} {key}: no entry")
            continue
        got = read(entry, int(sent))
    if got != want:
        bad.append(f"{kind} {key} sent {sent}: reads {got!r}, the FM-1 shows {want!r}")
for b in bad[:6]:
    print("  " + b)
print(f"  ({n} values: " + ", ".join(f"{m} {c}" for m, c in sorted(counts.items())) + ")")
check("every value sent reads exactly as the screen shows it", not bad and n > 400)
check("the tone rows by name (Program Change 0..4)", dev["presets"] == ["the Original", "morning stuff",
                                                                        "the Big Drone", "Schoenberg", "Bach"])
check("VIB RATE as 14-bit NRPN 0:76 (hires)", dev["NRPN"].get("76", {}).get("hires") == "true")
check("every entry labelled (MIDItoSpeech speaks only labelled ones)",
      all(e.get("label") for sec in ("CC", "NRPN") for e in dev[sec].values()))
print("synthaccess sidecar FAILED" if failed else "synthaccess sidecar passed")
sys.exit(1 if failed else 0)
