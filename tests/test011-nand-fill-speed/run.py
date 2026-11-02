#!/usr/bin/env python3
"""test011 automated: UFFS read and write speed with the shell's cp (5,181,000-byte leakcam_wake), 3 runs each,
byte-exact check by size, files removed after; 8 snapshots (leak verdict)."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402,F401

r = Run(__file__)
r.boot()
r.sample("idle")
idle = r.meter("K230 at msh idle (agent running)", force=True)
SRC, N = "/sdcard/app/leakcam_wake", 5181000


def timed(c):
    t0 = time.time()
    out = r.k(c, 60)
    return time.time() - t0, out


rd, wr = [], []
for i in range(3):
    t, _ = timed(f"cp {SRC} /tmp/t011")
    rd.append(t)
    t, out = timed("cp /tmp/t011 /sdcard/t011")
    wr.append(t)
    sz = re.search(r"t011\s+(\d+)", r.k("ls /sdcard", 3))
    if not sz or int(sz.group(1)) != N:
        r.obstacle(f"run {i + 1}: written file size {sz.group(1) if sz else 'none'} != {N}")
    r.k("rm /sdcard/t011", 10)
    r.k("rm /tmp/t011", 5)
    r.sample(f"run {i + 1}")
r.k(f"cp {SRC} /tmp/t011", 60)
busy = r.meter(force=True, state="K230 copying RAM -> /sdcard over and over (cp)",
               busy=lambda: (r.k("cp /tmp/t011 /sdcard/t011", 60), r.k("rm /sdcard/t011", 10)))
r.k("rm /sdcard/t011", 10)
r.k("rm /tmp/t011", 5)
for i in range(5):
    r.sample(f"after {i + 1}")
mbr, mbw = N / min(rd) / 1e6, N / min(wr) / 1e6
r.set("speed", f"read {mbr:.1f} MB/s, write {mbw:.1f} MB/s (shell cp, best of 3, host-timed incl. ~0.1 s prompt); "
               f"user read() 12.7 MB/s = 81 % of test017's raw 15.6 MB/s ({r.date})")
r.set("share", f"UFFS mounts in the boot: /bin + /sdcard done at 0.31 s (test022 boot_times) ({r.date})")
r.energy(busy, min(wr), per_day=4, idle_amps=idle, what="5 MB clip write")
r.finish(f"cp {SRC} -> RAM: {', '.join(f'{x:.2f}' for x in rd)} s; RAM -> /sdcard: {', '.join(f'{x:.2f}' for x in wr)} s; "
         f"read {mbr:.1f} MB/s, write {mbw:.1f} MB/s.", not r.obstacles)
