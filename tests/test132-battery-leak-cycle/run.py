#!/usr/bin/env python3
"""test132: the full firmware's leak checks on battery (DESIGN.md 5.3.12): the RTC's check and a wet probe each power
the K230 for its job, DONE, the K230 off; the result at the server at once on a change or a wet probe. Hands: USB-C 1
out (the BL616 console stays on its own port), the probe wet and dry, USB-C 1 back; everything else automated (no
phone). The HBN sleep between the events is not built yet: the firmware stays awake, so no sleep current here.
    tests/test132-battery-leak-cycle/run.py
"""
import json
import os
import re
import sys
import time
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import K230_PORT, log, openp   # noqa: E402
from phone import SERVER, board_checkin, board_id, firmware, hand, intervals   # noqa: E402

DIR = "/sdcard/leakcam"
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def plain(s):
    return re.sub(r"\x1b\[[0-9;]*m", "", s)


def metrics():
    with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}", timeout=5) as f:
        d = json.loads(f.read())
    return d.get("metrics", {}), d.get("last_seen_s", -1), d.get("charging")


def wait(pred, secs, step=2):
    t0 = time.time()
    while time.time() - t0 < secs:
        if pred():
            return time.time() - t0
        time.sleep(step)
    return None


def session(secs, why):
    """the BL616's console from now until the K230 goes off with `why` (a check's whole session); the lines"""
    return plain(bl("leak", secs, r"k230: off \(" + re.escape(why) + r"\)[^\n]*\n"))


def session_facts(out):
    """(DONE result, ms ask -> DONE, rails-on ms, off reason) from a session's console lines"""
    d = re.search(r"leak: DONE (\S+) (\d+) ms after the ask", out)
    o = re.search(r"k230: off \(([^)]*)\) (\d+) ms after power-on", out)
    return (d.group(1) if d else None, int(d.group(2)) if d else None, int(o.group(2)) if o else None,
            o.group(1) if o else None)


r = Run(__file__)
dev = board_id()
fw = firmware()
if fw != "full":
    r.finish(f"not run: the board runs {fw or 'nothing'}, not the full firmware", False, switch_off=False)
    sys.exit(1)

# 0. on USB: the scene for the battery checks (cam0 an object: each check reports a change), histories fresh
r.fd = openp(K230_PORT)
for c in (f"rm {DIR}/cam0.hist", f"rm {DIR}/cam1.hist", f"rm {DIR}/sim"):
    r.k(c, 2)
bl("leak check", 8, r"leak: DONE [^\n]*\n")             # histories written: new,new
r.k(f"echo cam0=object {DIR}/sim", 2)
intervals(dev)
wait(lambda: board_checkin() == 60, 70)

# 1. USB-C 1 out: the battery read at once, the K230 off, the server told (charging 0)
if hand("USB-C 1: pull the cable out (the board on its battery; the BL616 console cable stays)", "battery") is None:
    r.finish("not run: USB-C 1 never pulled", False, switch_off=False)
    sys.exit(1)
t = wait(lambda: metrics()[2] is False, 40)
m, _, _ = metrics()
check("unplugged: the server sees chg 0 at once, the battery read", t is not None and m.get("bat", 0) > 3000,
      f"{t:.0f} s, bat {m.get('bat')} mV" if t is not None else "no chg 0 in 40 s")

# 2. the RTC's check on battery: a K230 session `rtc`, DONE changed,same, the K230 off after it, at the server at once
intervals(dev, leakcheck=60)
t0 = time.time()
out = session(150, "leak check done")
res, ask_ms, on_ms, why = session_facts(out)
check("battery, the RTC's check: a K230 session rtc", "k230: session rtc" in out, "seen" if "session rtc" in out else "none")
check("battery, the RTC's check: DONE changed,same (cam0's object)", res == "changed,same", f"{res}, {ask_ms} ms after the ask")
check("battery, the RTC's check: the K230 off after DONE", why == "leak check done" and on_ms is not None,
      f"rails on {on_ms} ms" if on_ms else "no off line")
got = wait(lambda: metrics()[0].get("job") == "changed,same" and 0 <= metrics()[0].get("jago", -1) <= 30, 40)
check("battery, the RTC's check: the server has job=changed,same at once", got is not None,
      f"{got:.0f} s after DONE" if got is not None else "not in 40 s")
m, _, _ = metrics()
check("battery, the RTC's check: in the check-in ks >= 1, klast 1", m.get("ks", 0) >= 1 and m.get("klast") == 1,
      f"ks {m.get('ks')}, klast {m.get('klast')}, kms {m.get('kms')}")
if on_ms:
    r.set("Battery leak check: rails on", f"{on_ms} ms rails on per check (power-on -> off after DONE), DONE "
          f"{ask_ms} ms after the ask")
intervals(dev)                                            # no more RTC checks while the probe is tested

# 3. a wet probe: at once, a session `leak`, DONE, off; the probe low at the server
if hand("Probe: wet it (a wet towel across the probe pads)", "wet") is None:
    r.finish("stopped: the probe never wet", False, switch_off=False)
    sys.exit(1)
out = session(60, "leak check done")
res, ask_ms, on_ms2, why = session_facts(out)
check("wet probe: `leak: probe wet` and a K230 session leak", "leak: probe wet" in out and "k230: session leak" in out)
check("wet probe: DONE, the K230 off after it", res is not None and why == "leak check done", f"{res}, rails on {on_ms2} ms")
got = wait(lambda: 0 <= metrics()[0].get("probe", 9999) < 1100, 40)
check("wet probe: the server has probe < 1100 mV at once", got is not None,
      f"{metrics()[0].get('probe')} mV, {got:.0f} s" if got is not None else f"probe {metrics()[0].get('probe')}")

# 4. dry again
if hand("Probe: dry it (wipe the pads dry)", "dry") is None:
    r.finish("stopped: the probe never dry", False, switch_off=False)
    sys.exit(1)
out = plain(bl("leak", 20, r"probe dry"))
check("dry again: `leak` says probe dry", "probe dry" in out)

# 5. USB-C 1 back: the K230 on with USB power, the scene back to same
if hand("USB-C 1: plug the bench cable back in", "usb") is None:
    r.finish("stopped: USB-C 1 not back", False, switch_off=False)
    sys.exit(1)
t = wait(lambda: metrics()[2] is True, 40)
check("USB back: the server sees chg 1 at once", t is not None, f"{t:.0f} s" if t is not None else "not in 40 s")
time.sleep(5)
r.k(f"rm {DIR}/sim", 2)
r.set("Test run time", f"{time.time() - r.t0:.0f} s with the hand steps")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
