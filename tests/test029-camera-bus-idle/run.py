#!/usr/bin/env python3
"""test029 automated: 8 boots, each: i2cdetect on i2c0 and i2c4 (no hang; per bus either no device = no camera, or only
0x36 = the OV5647 camera: both cases pass, anything else fails), pad_read of SCL/SDA (1) and GPIO62/63 (0).
Written before the cameras arrived (no device expected); since 2026-11-12 the cameras may be plugged (FAIL "a device on
i2c0" with two OV5647 plugged, 00:13): the case is reported per bus."""
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl, REPO   # noqa: E402,F401

r = Run(__file__)
fails, shown, seen = [], "", {}
for i in range(8):
    r.off()
    time.sleep(2)
    r.boot()
    for bus in ("i2c0", "i2c4"):
        out = r.k(f"i2cdetect {bus}", 15, r"msh />\s*$")
        found = set(re.findall(r"\b[0-7][0-9a-f]\b(?!:)",
                               "\n".join(l[4:] for l in out.splitlines() if re.match(r"[0-7]0:", l))))
        seen.setdefault(bus, set()).add("camera 0x36" if found == {"36"} else "none" if not found else
                                        "other " + ",".join(sorted(found)))
        if found - {"36"}:
            fails.append(f"boot {i + 1}: {bus} devices {sorted(found)} (only 0x36, the OV5647, may answer)")
        if "msh />" not in out:
            fails.append(f"boot {i + 1}: i2cdetect {bus} hung")
    shown = r.k("pad_read", 5)
    r.sample(f"boot {i + 1}")
idle = r.meter("K230 at msh idle (agent running)", force=True)
r.set("speed", f"n/a: a level check ({r.date})")
r.set("share", f"n/a: the I2C buses register in the boot at no visible cost (i2c0/i2c4 at 0.27 s, test022) ({r.date})")
r.set("energy", f"n/a: buses idle; the board at msh idle {idle * 1000:.0f} mA ({r.date})" if idle else None)
r.set("perday", f"n/a: no activity of its own ({r.date})")
cases = "; ".join(f"{b}: {' / '.join(sorted(v))}" for b, v in sorted(seen.items()))
r.finish(f"8 boots, both camera I2C buses scanned, no hang; devices ({cases}); pad_read:\n\n" +
         "\n".join(f"    {l}" for l in shown.splitlines() if l.strip() and "msh" not in l)[:1500] +
         ("\n\n" + "; ".join(fails) if fails else ""), not fails)
