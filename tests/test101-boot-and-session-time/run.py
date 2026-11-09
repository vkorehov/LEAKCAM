#!/usr/bin/env python3
"""test101: the K230 session's boot and the leak check's job time on the product pair (firmware/bl616_full in slot 0,
bl616_setup in slot 1, the product K230 image), on USB power. The full firmware starts the K230 session at its own
start and logs its steps (k230_session.c): rails on (the 3V3 ramp), the program's first heartbeat edge, READY; on USB
the K230 then stays on (DESIGN.md 5.3.12) and a leak check is a CHECK to the running program (leak_check.c): the
ask -> DONE time. A cold session: `k230 off`, then the full firmware's own restart on USB power 30 s later (usb_full.c
RETRY_MS) with the console connected (after a BL616 reboot READY comes 0.8 s in, before the PC has the port again:
missed, 2026-11-08). One cold session and RUNS leak checks; --stress: RUNS cold sessions. No hands.
    tests/test101-boot-and-session-time/run.py [--stress]
"""
import os
import re
import statistics
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log   # noqa: E402
from phone import firmware   # noqa: E402

RUNS = 3
SESSIONS = RUNS if "--stress" in sys.argv else 1
checks, rows, jobs = [], [], []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def clean(t):
    return re.sub(r"\x1b\[[0-9;]*m", "", t)


def session():
    """`k230 off`, the restart on USB power: the K230 session's logged steps (ms after the run), or None"""
    out = clean(bl("k230 off", 3))
    t0 = time.time()
    while time.time() - t0 < 45 and not re.search(r"READY \d+ ms after the run", out):
        out += clean(bl("", 3, r"READY \d+ ms after the run"))
    ramp = re.search(r"rails on \(3V3 in (\d+) us\)", out)
    edge = re.search(r"first heartbeat edge (\d+) ms after the run", out)
    ready = re.search(r"READY (\d+) ms after the run", out)
    if not ready:
        return None
    return {"ready": int(ready.group(1)), "edge": int(edge.group(1)) if edge else None,
            "ramp_us": int(ramp.group(1)) if ramp else None}


r = Run(__file__)
if firmware() != "full":
    r.finish(f"not run: the board runs {firmware() or 'nothing'}, not the full firmware", False, switch_off=False)
    sys.exit(1)

# 1. power-on -> READY: the K230 session at the full firmware's start
for i in range(SESSIONS):
    s = session()
    check(f"session {i + 1}: READY logged", s is not None, str(s))
    if s:
        rows.append(s)
    r.sample(f"after session {i + 1}")

# 2. one leak check on USB: CHECK to the running program -> DONE
for i in range(RUNS):
    out = clean(bl("leak check", 30, r"leak: DONE [^\n]*ms after the ask|no DONE"))
    m = re.search(r"leak: DONE (\S+) (\d+) ms after the ask", out)
    check(f"leak check {i + 1}: DONE from the running K230", m is not None, m.group(0) if m else out[-120:])
    if m:
        jobs.append(int(m.group(2)))
r.sample("after the leak checks")

if rows:
    med = lambda k: statistics.median(x[k] for x in rows if x.get(k) is not None)
    edges = [x for x in rows if x.get("edge") is not None]
    r.set("boot", f"run -> READY {med('ready') / 1000:.2f} s (median of {len(rows)}; first heartbeat edge "
                  f"{(med('edge') / 1000) if edges else float('nan'):.2f} s; the product pair on USB, {r.date})")
if jobs:
    r.set("share", f"a leak check on USB (CHECK to the running K230 -> DONE): {statistics.median(jobs) / 1000:.2f} s "
                   f"(median of {len(jobs)}); on battery the session adds the boot above and rails off")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
