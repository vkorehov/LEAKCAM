#!/usr/bin/env python3
"""test102 automated, on USB power (the USB-C 1 VBUS meter; no battery measurements, user 2026-10-30): the one state
no test has read - a K230 session while the agent works (test101: 24.5 s after READY) - read while sessions repeat
(`pt_session`, LEAKCAM_METER=ask); every other state from the readings already taken (never read twice). Energy per
wake = the states over test101's session timeline."""
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402

r = Run(__file__)
# recorded states (A at 5 V on USB-C 1), with their source
KNOWN = [("rails off, BL616 awake", 0.061, "test006"), ("K230 rails on, held in reset", 0.087, "test021"),
         ("K230 at msh idle (agent running)", 0.220, "test006"), ("KPU loop", 0.220, "test013"),
         ("encoder 2 x 15 fps", 0.235, "test027: +15 mA"), ("both LED chains 100 %", 0.95, "test009: +0.73 A")]
# test101's timeline (median of 10): READY 0.87 s, the agent's last heartbeat 25.36 s, rails off 27.68 s
BOOT_S, WORK_S, TAIL_S = 0.87, 25.36 - 0.87, 27.68 - 25.36

r.off()
bl("pt_boot1 low")
work = r.meter("K230 session, the agent working (no camera, no server)", force=True,
               busy=lambda: (r.off(), bl("pt_session 60000", 60, r"rails off [^\n]*\n")))
r.sample("after the sessions")
rows = "; ".join(f"{n} {a * 1000:.0f} mA ({src})" for n, a, src in KNOWN)
if work is not None:
    q = 0.087 * BOOT_S + work * WORK_S + 0.220 * TAIL_S      # A*s at 5 V on USB
    r.set("current", f"session, agent working: {work * 1000:.0f} mA (new reading); {rows} ({r.date})")
    r.set("energy", f"one wake (test101 timeline: boot 0.87 s at ~87 mA, agent {WORK_S:.1f} s at {work * 1000:.0f} mA, "
                    f"{TAIL_S:.1f} s at 220 mA): {q:.2f} A*s = {q / 3.6:.2f} mAh at the board, {q * 5:.1f} J from USB "
                    f"({r.date})")
    r.set("perday", f"4 report sessions a day (6 h, DESIGN 5.1): {4 * q / 3.6:.1f} mAh/day at the board ({r.date})")
r.set("voltage", "n/a: on USB power; no battery measurements (user, 2026-10-30)")
r.set("share", f"n/a: the whole session is the subject (test101 has the steps) ({r.date})")
r.finish(f"session current read on USB: {work * 1000:.0f} mA while the agent works" if work is not None
         else "no meter reading given.", work is not None)
