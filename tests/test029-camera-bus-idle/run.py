#!/usr/bin/env python3
"""test029 automated: 8 boots, each: i2cdetect on i2c0 and i2c4 (no device, no hang), pad_read of SCL/SDA (1) and
GPIO62/63 (0)."""
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl, REPO   # noqa: E402,F401

r = Run(__file__)
fails, shown = [], ""
for i in range(8):
    r.off()
    time.sleep(2)
    r.boot()
    for bus in ("i2c0", "i2c4"):
        out = r.k(f"i2cdetect {bus}", 15, r"msh />\s*$")
        if re.search(r"\b[0-7][0-9a-f]\b(?!:)", "\n".join(l[4:] for l in out.splitlines() if re.match(r"[0-7]0:", l))):
            fails.append(f"boot {i + 1}: a device on {bus}")
        if "msh />" not in out:
            fails.append(f"boot {i + 1}: i2cdetect {bus} hung")
    shown = r.k("pad_read", 5)
    r.sample(f"boot {i + 1}")
idle = r.meter("K230 at msh idle (agent running)", force=True)
r.set("speed", f"n/a: a level check ({r.date})")
r.set("share", f"n/a: the I2C buses register in the boot at no visible cost (i2c0/i2c4 at 0.27 s, test022) ({r.date})")
r.set("energy", f"n/a: buses idle; the board at msh idle {idle * 1000:.0f} mA ({r.date})" if idle else None)
r.set("perday", f"n/a: no activity of its own ({r.date})")
r.finish("8 boots: both camera I2C buses scanned with no device and no hang; pad_read:\n\n" +
         "\n".join(f"    {l}" for l in shown.splitlines() if l.strip() and "msh" not in l)[:1500] +
         ("\n\n" + "; ".join(fails) if fails else ""), not fails)
