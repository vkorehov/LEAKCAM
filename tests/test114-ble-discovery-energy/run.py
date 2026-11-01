#!/usr/bin/env python3
"""test114 automated, part A: how long the phone needs to find the board at each advertising interval (BL616 test
firmware, `pt_ble adv <ms>`), 10 discoveries per interval (BleOp scan: ms from the scan start to the first
advertisement). Android drops scans from an app that starts more than 5 in 30 s (test111): the scans are spaced 7 s
apart. The current per interval: the USB-C 1 meter with the cell full, on cue (LEAKCAM_METER=ask), else latency only.
Parts B (the board listens for the app's wake beacon) and C (BLE sleep between radio events) follow (README).
    tests/test114-ble-discovery-energy/run.py
"""
import fcntl
import os
import statistics
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log   # noqa: E402
from phone import op, ready   # noqa: E402

INTERVALS_MS = (100, 500, 1000, 2000, 5000, 10240)
TRIES = 10
SPACING_S = 7                    # at most 5 scan starts in 30 s (Android)

_lock = open(__file__)
try:
    fcntl.flock(_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except OSError:
    sys.exit("test114: another run.py is running")

r = Run(__file__)
if not os.path.exists(BL_PORT) or "pt_ble" not in bl("help", 2):
    r.obstacle("the BL616 test firmware with pt_ble is needed (tests/firmware/bl616_powertest)")
    r.finish("not run: no pt_ble.", False, switch_off=False)
    sys.exit(1)
why = ready(r.notes)
if why:
    r.obstacle(why)
    r.finish(f"not run: {why.split(' (')[0]}.", False, switch_off=False)
    sys.exit(1)
bl("pt_ble on", 5, r"starting|already on")
time.sleep(3)
rows, last = [], 0.0
for ms in INTERVALS_MS:
    out = bl(f"pt_ble adv {ms}", 2, r"advertising every|adv:")
    if f"advertising every {ms} ms" not in out:
        r.obstacle(f"pt_ble adv {ms} not taken: {out.strip()[-80:]}")
        continue
    amps = r.meter(f"advertising every {ms} ms, BLE idle otherwise")
    seen = []
    for _ in range(TRIES):
        time.sleep(max(0.0, SPACING_S - (time.time() - last)))
        last = time.time()
        res = op("scan", secs=max(15, 2 * ms // 1000 + 5))
        if res.get("found"):
            seen.append(res["ms"])
    med = statistics.median(seen) if seen else None
    rows.append((ms, len(seen), med, max(seen) if seen else None, amps))
    log(f"adv {ms} ms: found {len(seen)}/{TRIES}, median {med} ms, max {max(seen) if seen else '-'} ms, "
        f"current {amps if amps is not None else 'not read'}")
bl("pt_ble adv 100", 2)
lines = [f"{ms} ms: {n}/{TRIES} found, median {med} ms, max {mx} ms" + (f", {a * 1000:.1f} mA" if a else "")
         for ms, n, med, mx, a in rows]
r.set("speed", "; ".join(lines))
ok = len(rows) == len(INTERVALS_MS)          # the sweep itself is the result: misses at long intervals are findings
r.finish("Advertising interval -> phone discovery: " + "; ".join(lines) + ".", bool(ok), switch_off=False)
sys.exit(0 if ok else 1)
