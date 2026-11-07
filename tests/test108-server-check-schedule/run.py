#!/usr/bin/env python3
"""test108: the server drives the check-in and leak check intervals per device (DESIGN.md 5.3.12), on USB power at the
bench, no hand: PUT /v1/devices/<id>/intervals, the board's `presence` / `leak`, the server's last_seen_s, the K230's
CHECK lines.
    tests/test108-server-check-schedule/run.py
"""
import json
import os
import re
import sys
import time
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import K230_PORT, cmd, log, openp   # noqa: E402
from phone import SERVER, board_checkin, board_id, firmware, intervals   # noqa: E402

checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def leak_state():
    """(interval s, checks, last result) from the BL616's `leak`"""
    m = re.search(r"every (\d+) s, next in \d+ s, checks (\d+), wet edges \d+, last (\S+?)(,|\r|\n|$)",
                  re.sub(r"\x1b\[[0-9;]*m", "", bl("leak", 2)))
    return (int(m.group(1)), int(m.group(2)), m.group(3)) if m else (None, None, None)


def last_seen():
    with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}", timeout=5) as f:
        return json.loads(f.read()).get("last_seen_s", -1)


def check_in_times(n, secs):
    """the times of the next n check-ins (the server's last_seen_s falling back), within secs"""
    t0, got, prev = time.time(), [], None
    while time.time() - t0 < secs and len(got) < n:
        s = last_seen()
        if prev is not None and s < prev:
            got.append(time.time())
        prev = s
        time.sleep(0.5)
    return got


def wait(pred, secs, step=2):
    t0 = time.time()
    while time.time() - t0 < secs:
        if pred():
            return time.time() - t0
        time.sleep(step)
    return None


def bl_free():
    m = re.search(r"free (\d+) largest \d+ min (\d+)", bl("mem", 1.5))
    return (int(m.group(1)), int(m.group(2))) if m else None


r = Run(__file__)
dev = board_id()
fw = firmware()
if fw != "full":
    r.finish(f"not run: the board runs {fw or 'nothing'}, not the full firmware", False, switch_off=False)
    sys.exit(1)
r.fd = openp(K230_PORT)
mem0 = bl_free()
intervals(dev)                               # the defaults (a run before may have left an override)
wait(lambda: board_checkin() == 60, 70)

# 1. the defaults
every, n0, _ = leak_state()
check("defaults: check-in every 60 s (the bench server's), leak check every 86400 s",
      board_checkin() == 60 and every == 86400, f"check-in {board_checkin()} s, leak {every} s")

# 2. the check-in interval per device
t0 = time.time()
intervals(dev, checkin=20)
took = wait(lambda: board_checkin() == 20, 70)
times = check_in_times(3, 80)
gaps = [round(b - a) for a, b in zip(times, times[1:])]
check("PUT checkin 20: the board on it within one old interval (60 s)", took is not None,
      f"{took:.0f} s after the PUT" if took is not None else "not in 70 s")
check("PUT checkin 20: check-ins ~20 s apart", len(gaps) == 2 and all(15 <= g <= 30 for g in gaps), f"gaps {gaps} s")
if took is not None:
    r.set("Interval taken", f"check-in 20 s taken {took:.0f} s after the PUT; check-ins then {gaps} s apart")

# 3. the leak check interval per device: a check by itself within ~60 s
intervals(dev, checkin=20, leakcheck=60)
took = wait(lambda: leak_state()[0] == 60, 40)
check("PUT leakcheck 60: the board on it at the next check-in", took is not None,
      f"{took:.0f} s after the PUT" if took is not None else "not in 40 s")
_, n1, _ = leak_state()
r.console = ""
t1 = time.time()
first = wait(lambda: (leak_state()[1] or 0) > n1, 75, step=1)
t_first = time.time()
r.console += cmd(r.fd, None, 2)
every, n2, last = leak_state()
check("a leak check by itself within ~60 s (the RTC check)", first is not None and last not in (None, "none"),
      f"{first:.0f} s after the interval was taken, result {last}" if first is not None else "none in 75 s")
check("its CHECK reached the running K230 (job: CHECK rtc), the K230 still on", "job: CHECK rtc" in r.console
      and "job: done" in r.console, "seen" if "job: CHECK rtc" in r.console else "no CHECK line")

# 4. again on its interval
second = wait(lambda: (leak_state()[1] or 0) > n2, 80, step=1)
spacing = time.time() - t_first if second is not None else None
check("the next leak check 60 s +- 5 s later", spacing is not None and 55 <= spacing <= 65,
      f"{spacing:.0f} s" if spacing is not None else "none in 80 s")
if spacing is not None:
    r.set("Leak check on its interval", f"{spacing:.0f} s between two RTC checks at leakcheck 60")

# 5. back to the defaults
intervals(dev)
took = wait(lambda: board_checkin() == 60 and leak_state()[0] == 86400, 40)
check("PUT 0: back on 60 s / 86400 s", took is not None, f"{took:.0f} s" if took is not None else "not in 40 s")
mem1 = bl_free()
check("the BL616's free heap kept", mem0 and mem1 and mem1[0] >= mem0[0] - 1024, f"{mem0} -> {mem1}")
r.set("Memory BL616", f"BL616 free {mem0} -> {mem1} (free, min); the K230 job's heap: test049")
r.set("Test run time", f"{time.time() - r.t0:.0f} s automated")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
