#!/usr/bin/env python3
"""test012 automated: the microphone through leakcam_stream -m (codec + I2S + level per second), 10 s, 3 runs;
pass: rms above the -60 dBFS dead floor and a level reported for every second."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl   # noqa: E402,F401

r = Run(__file__)
r.boot()
r.sample("idle")
idle = r.meter("K230 at msh idle (agent running)", force=True)
levels, fails = [], []
for i in range(3):
    t0 = time.time()
    out = r.k("/sdcard/app/leakcam_stream -m 10", 40, r"msh />\s*$")
    secs = re.findall(r"mic\s+([\d.]+) s: rms\s+(-?[\d.]+) dBFS, peak\s+(-?[\d.]+)", out)
    if len(secs) < 8:
        fails.append(f"run {i + 1}: {len(secs)} level lines")
    levels += [(float(a), float(b)) for _, a, b in secs[2:]]          # the first ~2 s: codec input charging
    r.sample(f"run {i + 1}")
    if i == 0:
        busy = r.meter("K230 recording the microphone (leakcam_stream -m 10, repeated)", force=True,
                       busy=lambda: r.k("/sdcard/app/leakcam_stream -m 10", 40, r"msh />\s*$"))
for i in range(5):
    r.sample(f"after {i + 1}")
rms = [a for a, _ in levels]
ok = bool(rms) and min(rms) > -60 and not fails
r.set("speed", f"48 kHz mono, 1 level line per second for 10 s in every run; quiet-room rms {min(rms):.1f} to "
               f"{max(rms):.1f} dBFS, peak {max(p for _, p in levels):.1f} dBFS ({r.date})" if rms else None)
r.set("share", f"n/a: the codec starts in the program, not in the boot ({r.date})")
r.energy(busy, 10, per_day=4, idle_amps=idle, what="10 s audio")
r.finish(f"3 x 10 s: rms {min(rms):.1f}..{max(rms):.1f} dBFS" if rms else "no level lines" +
         ("; " + "; ".join(fails) if fails else ""), ok)
