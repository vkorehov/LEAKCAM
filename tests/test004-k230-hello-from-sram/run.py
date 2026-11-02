#!/usr/bin/env python3
"""test004 automated: USB boot, k230_hello loaded into SRAM (make run), the program toggles K230_ALIVE once a second:
pt_alive 10 counts it (the UART0 lines would need the CH340, which the bring-up no longer uses)."""
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl, REPO, KDIMG   # noqa: E402,F401

r = Run(__file__)
hello = os.path.join(REPO, "tests", "firmware", "k230_hello")
res, fails = [], []
for i in range(3):
    r.boot(boot1="high", until=b"zzz", secs=3, expect_msh=False)
    for _ in range(20):
        if "29f1:0230" in subprocess.run(["lsusb"], capture_output=True, text=True).stdout:
            break
        time.sleep(0.5)
    t0 = time.time()
    p = subprocess.run(["make", "-C", hello, "run"], capture_output=True, text=True, timeout=120)
    load = time.time() - t0
    out = bl("pt_alive 10", 13, r"first edge[^\n]*\n|edges 0[^\n]*\n")
    hp = re.search(r"half-period min (\d+) / avg (\d+) / max (\d+) ms", out)
    e = re.search(r"edges (\d+)", out)
    if not e or int(e.group(1)) < 8:
        fails.append(f"run {i + 1}: {e.group(1) if e else 'no'} K230_ALIVE edges in 10 s")
    res.append((load, hp.group(2) if hp else "?"))
    r.sample(f"run {i + 1}")
r.off()
bl("pt_boot1 low")
r.set("boot", f"n/a: USB boot of a bare-metal program; load + start {min(x[0] for x in res):.1f} s (make run, host) "
      f"({r.date})")
r.set("share", f"n/a: no RT-Smart boot ({r.date})")
r.set("speed", f"K230_ALIVE half-period avg {', '.join(x[1] for x in res)} ms (program: 1000 ms per line) ({r.date})")
r.set("perday", f"n/a: bring-up program, never in a session ({r.date})")
r.set("energy", f"n/a: bring-up program ({r.date})")
r.finish(f"3 USB boots: k230_hello loaded in {', '.join(f'{x[0]:.1f}' for x in res)} s, K230_ALIVE toggling (avg half-"
         f"period {', '.join(x[1] for x in res)} ms)" + ("; " + "; ".join(fails) if fails else "."), not fails)
