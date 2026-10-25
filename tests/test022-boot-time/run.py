#!/usr/bin/env python3
"""test022 automated: 8 cold boots from NAND (rails off 5 s each), host milestones + the kernel's `boot_times`, a
snapshot after every boot (memory over 8 boots = the leak verdict)."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run   # noqa: E402

BOOTS = 8
r = Run(__file__)
app, env = [], []
for i in range(BOOTS):
    r.off()
    time.sleep(5)
    r.boot()
    out = r.k("boot_times", 3)
    m = re.search(r"([\d.]+) s\s+app init done", out)
    e = re.search(r"([\d.]+) s\s+env init done", out)
    if m:
        app.append(float(m.group(1)))
        env.append(float(e.group(1)) if e else None)
    r.sample(f"boot {i + 1}")
ok = len(app) == BOOTS
app.sort()
res = (f"{len(app)} of {BOOTS} cold boots reached msh with `boot_times`: reset release -> app init "
       f"{app[0]:.3f} / {app[len(app) // 2]:.3f} / {app[-1]:.3f} s (min / median / max); mounts done at "
       f"{min(x for x in env if x):.3f}-{max(x for x in env if x):.3f} s." if app else "no boot_times output")
r.finish(res, ok)
