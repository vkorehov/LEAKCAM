#!/usr/bin/env python3
"""test110 automated: the LEAKCAM Setup app against loss at every step of a setup. The BL616 test firmware injects the
loss (pt_fault: notifications dropped, the link dropped at connect / each check state / the result, DNS or HTTP
answers lost); the phone's own Bluetooth is switched off during a check over adb. Per case: what the app told the
user, how long it took, whether that matches what the board stored (consistency), and a clean setup right after
(recovery). Needs the BL616 test firmware with pt_fault, USB-C 1 in, the phone on adb, unlocked.
    tests/test110-ble-loss-recovery/run.py
"""
import fcntl
import os
import re
import sys
import threading
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log, wifi_credentials   # noqa: E402
from phone import adb, creds, op, ready, want   # noqa: E402

DECOY = ("t110-decoy", "password1")   # stored unchecked before each case: a stored real network then means "stored"
FAIL_MS = 35                          # a failure must be told by then, not by the app's 60 s step timeout

_lock = open(__file__)
try:
    fcntl.flock(_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except OSError:
    sys.exit("test110: another run.py is running")

r = Run(__file__)
checks, rows = [], []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def setup(s, k, anyway=False):
    res = op("app", ssid=s.encode().hex(), psk=k.encode().hex(), anyway="1" if anyway else "", secs=150)
    helps = res.get("help", [])
    last = helps[-1]["help"] if helps else res.get("error", "no help text")
    secs = res.get("ms_end", 0) / 1000
    log("setup", s if s != ssid else "<network>", f"{secs:.1f} s", " | ".join(h["help"][:60].replace("\n", " ")
                                                                            for h in helps[-2:]))
    return last, secs, res


def fault(f):
    return bl(f"pt_fault {f}", 2)


def store_decoy():
    last, _, _ = setup(*DECOY, anyway=True)
    return last.startswith("Saved without checking.") and creds() == want(*DECOY)


def recover(case):
    """the fault off, a clean setup: Done and the real network stored"""
    fault("off")
    last, secs, _ = setup(ssid, psk)
    check(f"{case}: clean setup after it Done", last.startswith("Done.") and creds() == want(ssid, psk),
          f"{secs:.1f} s")


def case(name, f, expect, during=None):
    """expect: 'done' (the app recovers by itself) or 'fail' (a named failure, in time, matching the board)"""
    if not store_decoy():
        check(f"{name}: decoy stored first", False)
        return
    fault(f)
    t = threading.Thread(target=during, daemon=True) if during else None
    if t:
        t.start()
    last, secs, res = setup(ssid, psk)
    if t:
        t.join()
    m = re.search(r"fired (\d+)", fault(""))
    fired = m.group(1) if m else "?"
    stored_real = creds() == want(ssid, psk)
    said_done = last.startswith("Done.")
    said_nothing = "Nothing was stored" in last
    rows.append((name, f"{secs:.1f}", "Done" if said_done else last.split(".")[0][:60], "real" if stored_real else
                 "decoy", fired))
    if expect == "done":
        check(f"{name}: app recovers, Done", said_done, f"{secs:.1f} s, fault fired {fired}")
    else:
        check(f"{name}: failure told in time", not said_done and secs < FAIL_MS, f"{secs:.1f} s: {last[:70]}")
    check(f"{name}: app and board agree", said_done == stored_real and not (said_nothing and stored_real),
          f"app {'Done' if said_done else 'not stored' if said_nothing else 'failure'}, board "
          f"{'real network' if stored_real else 'decoy'}")
    recover(name)


def bt_off_during_check():
    """the phone's Bluetooth off while the board checks: pairing and writes take about 5 s from opening the app"""
    time.sleep(6.5)
    adb("shell", "cmd", "bluetooth_manager", "disable")
    time.sleep(4)
    adb("shell", "cmd", "bluetooth_manager", "enable")


ssid, psk = wifi_credentials()
if not os.path.exists(BL_PORT):
    r.obstacle("no BL616 console on USB: reconnect, then run again")
    r.finish("not run: no BL616 console on USB.", False, switch_off=False)
    sys.exit(1)
if "pt_fault" not in bl("help", 2):
    r.obstacle("the BL616 firmware on the board has no pt_fault: flash tests/firmware/bl616_powertest")
    r.finish("not run: no pt_fault in the BL616 firmware.", False, switch_off=False)
    sys.exit(1)
why = ready(r.notes)
if why:
    r.obstacle(why)
    r.finish(f"not run: {why.split(' (')[0]}.", False, switch_off=False)
    sys.exit(1)
fault("off")
bl("pt_ble on", 5, r"advertising as|already on")
last, secs, _ = setup(ssid, psk)
check("baseline: Done without faults", last.startswith("Done."), f"{secs:.1f} s")

# lost notifications: the app reads STATUS as well, so each must still end in Done
case("every notification lost", "notify all", "done")
case("every second notification lost", "notify half", "done")
case("the result notification lost", "notify final", "done")
# the link lost at each step: a named failure, never a claim the board contradicts
case("link lost at connect", "link connect", "fail")
case("link lost when the check starts", "link joining", "fail")
case("link lost while getting an address", "link address", "fail")
case("link lost while reaching the internet", "link internet", "fail")
case("link lost with the result", "link result", "fail")
# Wi-Fi answers lost on the board's side: named, the old network kept
case("DNS answer lost", "dns", "fail")
case("HTTP answer lost", "http", "fail")
# the phone's Bluetooth off during the check
case("phone Bluetooth off during the check", "off", "fail", during=bt_off_during_check)
r.sample("after all cases")

failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" if d else n for n, _, d in failed) + "."
r.set("speed", "; ".join(f"{n}: {t} s, {said}, board {st}" for n, t, said, st, _ in rows))
log("cases (name, s, app said, board stored, fault fired):")
for row in rows:
    log("  ", " | ".join(row))
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
