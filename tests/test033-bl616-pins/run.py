#!/usr/bin/env python3
"""test033 automated: pt_pins in the 5 states (off, on_reset, running, boot1_high, uart_on), each checked by
pin_check.py against the pin table."""
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl, REPO   # noqa: E402,F401

r = Run(__file__)
check = os.path.join(REPO, "tools", "power-bringup", "pin_check.py")
res, fails = [], []


def dump(state):
    out = re.sub(r"\x1b\[[0-9;]*m", "", bl("pt_pins", 4, r"pin 34[^\n]*\n"))   # the shell colours every line
    f = os.path.join(r.dir, f"pins_{state}.txt")
    open(f, "w").write(out)
    p = subprocess.run([sys.executable, check, state, f], capture_output=True, text=True)
    last = p.stdout.strip().splitlines()[-1] if p.stdout.strip() else "no output"
    res.append(f"{state}: [pins_{state}.txt](pins_{state}.txt): {last.rsplit(': ', 1)[-1]}")
    if p.returncode:
        fails.append(state)
    r.sample(state)


r.off(); time.sleep(1); dump("off")
bl("pt_boot1 low"); bl("pt_on", 2); dump("on_reset")
r.boot(); dump("running")
r.boot(boot1="high", until=b"zzz", secs=3, expect_msh=False); dump("boot1_high")
r.off(); bl("pt_boot1 low"); bl("pt_on", 2); bl("pt_uart on", 1); dump("uart_on")   # rails on, K230 held in reset
r.set("boot", f"n/a: a pin check; the boot is test022 ({r.date})")
r.set("share", f"n/a: pin init is part of the BL616's main() (test021) ({r.date})")
r.set("speed", f"n/a: a pin check ({r.date})")
r.set("energy", f"n/a: pins cost nothing measurable (test031 pull audit) ({r.date})")
r.set("perday", f"n/a: as energy ({r.date})")
r.finish("pt_pins + pin_check.py:\n\n" + "\n".join(f"- {x}" for x in res), not fails)
