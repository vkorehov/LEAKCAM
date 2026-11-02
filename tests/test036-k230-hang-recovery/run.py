#!/usr/bin/env python3
"""test036 automated: pt_session cases, 3 of each: normal, hang at 5 s, reboot pulse at 5 s, no boot (15 s timeout);
rails off every time, times recorded."""
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl, REPO   # noqa: E402,F401

r = Run(__file__)
cases = [("normal", "pt_session 15000"), ("hang", "pt_session 15000 hang 5000"),
         ("reboot", "pt_session 15000 reboot 5000"), ("noboot", "pt_session 15000 noboot")]
out_t, fails = {}, []
for name, c in cases:
    for i in range(3):
        r.off()
        time.sleep(2)
        out = bl(c, 60, r"rails off [^\n]*\n")
        m = re.search(r"rails off (\d+) ms after power-on", out)
        f = re.search(r"first heartbeat edge (\d+) ms", out)
        if not m:
            fails.append(f"{name} {i + 1}: rails not reported off")
        else:
            out_t.setdefault(name, []).append((int(m.group(1)), int(f.group(1)) if f else None))
        if "ALIVE now 1" in out:
            fails.append(f"{name} {i + 1}: ALIVE still high after rails off")
        r.sample(f"{name} {i + 1}")
st = bl("pt_status", 2)
if "0 of 5 SDIO pull-ups high" not in st:
    fails.append("rails not off at the end")
fmt = lambda v: f"{min(v) / 1000:.2f}-{max(v) / 1000:.2f} s"
summ = "; ".join(f"{k}: rails off {fmt([a for a, _ in v])}" + (f", first heartbeat {fmt([b for _, b in v if b])}"
                                                                if any(b for _, b in v) else "")
                 for k, v in out_t.items())
if "hang" in out_t:
    r.set(r"time to rails off after a hang", f"{min(a for a, _ in out_t['hang']) / 1000 - 5:.2f}-"
          f"{max(a for a, _ in out_t['hang']) / 1000 - 5:.2f} s after the hang at 5 s ({r.date})")
if "normal" in out_t:
    fb = [b for _, b in out_t["normal"] if b]
    r.set("boot", f"power-on -> first heartbeat (READY stand-in) {fmt(fb)} (3 sessions) ({r.date})" if fb else None)
r.set("share", f"n/a: supervision runs on the BL616 beside the boot ({r.date})")
r.set("energy", f"a hang costs the msh-idle current (220 mA, test006) for 2.3 s of detection + discharge: "
                f"{0.22 * 2.3 / 3.6 * 1000:.0f} uAh per hang ({r.date})")
r.set("perday", f"n/a: hangs are faults, not daily events ({r.date})")
r.finish(f"12 sessions: {summ}." + (" " + "; ".join(fails) if fails else ""), not fails, switch_off=True)
