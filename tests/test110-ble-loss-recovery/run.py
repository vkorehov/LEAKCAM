#!/usr/bin/env python3
"""test110 automated: the LEAKCAM Setup app against loss at every step of a setup. The setup firmware's test bed
(tests/firmware/bl616_setup_test: firmware/bl616_setup's own sources, started by `lp_boot`, its switch to the full
firmware off) injects the loss (pt_fault: notifications dropped, the link dropped at connect / each check state / the result, DNS or HTTP
answers lost); the phone's own Bluetooth is switched off during a check over adb. Per case: what the app told the
user, how long it took, whether that matches what the board stored (consistency), and a clean setup right after
(recovery). Needs the setup test bed flashed (make flash there), USB-C 1 in, the phone on adb, unlocked.
    tests/test110-ble-loss-recovery/run.py
"""
import fcntl
import os
import re
import subprocess
import sys
import threading
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log, wifi_credentials   # noqa: E402
from phone import adb, creds, firmware, op, ready, want   # noqa: E402

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
    res = op("app", ssid=s.encode().hex(), psk=k.encode().hex(), anyway="1" if anyway else "", secs=60)
    helps = res.get("help", [])
    last = helps[-1]["help"] if helps else res.get("error", "no help text")
    secs = res.get("ms_end", 0) / 1000
    log("setup", s if s != ssid else "<network>", f"{secs:.1f} s",
        " | ".join(h["help"][:60].replace("\n", " ").replace(ssid, "<network>") for h in helps[-2:]))
    return last, secs, res


def fault(f):
    return bl(f"pt_fault {f}", 2)


def store_decoy():
    """written by the test firmware directly (pt_ble decoy): a save through the app took 16 s a case"""
    return "decoy stored" in bl("pt_ble decoy", 2) and creds() == want(*DECOY)


def recover(case):
    """the fault off, a clean setup: Done and the real network stored"""
    fault("off")
    last, secs, _ = setup(ssid, psk)
    check(f"{case}: clean setup after it Done", last.startswith("Done.") and creds() == want(ssid, psk),
          f"{secs:.1f} s")


def case(name, f, expect, during=None):
    """expect: 'done' (the app recovers by itself: STATUS reads, reconnect and read after a lost link) or 'fail' (a
    named failure, in time, matching the board)"""
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
    if not said_done:                    # a case that ended in Done was itself a full working setup
        recover(name)


def bt_off_for(secs):
    adb("shell", "cmd", "bluetooth_manager", "disable")
    time.sleep(secs)
    adb("shell", "cmd", "bluetooth_manager", "enable")


def bt_off_at(text):
    """the phone's Bluetooth off for 4 s once the app's screen log (it logs every screen) shows text: not a fixed delay -
    a faster setup finished before a 6.5 s delay, and the known board's setup was past the save 2 s after the open
    (2026-11-01)"""
    adb("logcat", "-c")
    log_ = subprocess.Popen(["adb", "logcat", "-s", "LeakcamSetup:I"], stdout=subprocess.PIPE, text=True)
    t0 = time.time()
    try:
        for line in log_.stdout:
            if text in line or time.time() - t0 > 40:
                break
    finally:
        log_.kill()
    bt_off_for(4)


def bt_off_during_check():
    bt_off_at("is now joining")


def bt_off_before_save():
    """at the app's first screen, while it connects: nothing sent yet"""
    bt_off_at("step ")


ssid, psk = wifi_credentials()
if not os.path.exists(BL_PORT):
    r.obstacle("no BL616 console on USB: reconnect, then run again")
    r.finish("not run: no BL616 console on USB.", False, switch_off=False)
    sys.exit(1)
if firmware() != "setup_test":
    r.obstacle(f"the BL616 runs {firmware()}: flash tests/firmware/bl616_setup_test (make flash)")
    r.finish("not run: not the setup test bed.", False, switch_off=False)
    sys.exit(1)
why = ready(r.notes)
if why:
    r.obstacle(why)
    r.finish(f"not run: {why.split(' (')[0]}.", False, switch_off=False)
    sys.exit(1)
fault("off")
bl("switch_to_full off", 2)
check("the setup firmware started (lp_boot)", re.search(r"lp: boot done|already run", bl("lp_boot", 20,
                                                                                         r"lp: boot done|already run")))
last, secs, _ = setup(ssid, psk)
check("baseline: Done without faults", last.startswith("Done."), f"{secs:.1f} s")

# lost notifications: the app reads STATUS as well, so each must still end in Done
case("every notification lost", "notify all", "done")
case("every second notification lost", "notify half", "done")
case("the result notification lost", "notify final", "done")
# the link lost on every connect: retried, then a named failure; nothing reached the board
case("link lost at every connect", "link connect", "fail")
# the link lost after the commit (with the commit write's answer, during each check state, with the result): the board
# goes on and stores; the app reconnects, reads STATUS for its attempt and shows the board's result
case("link lost when the check starts", "link joining", "done")
case("link lost while getting an address", "link address", "done")
case("link lost while reaching the internet", "link internet", "done")
case("link lost with the result", "link result", "done")
# Wi-Fi answers lost on the board's side: named, the old network kept
case("DNS answer lost", "dns", "fail")
case("HTTP answer lost", "http", "fail")
case("API answer lost (the internet answers)", "api", "fail")   # NO_API: the app names the LEAKCAM service
# the phone's Bluetooth off for 4 s: before the save a named failure (nothing sent); during the check the app sees the
# adapter go and reconnects when it is back
case("phone Bluetooth off before the save", "off", "fail", during=bt_off_before_save)
case("phone Bluetooth off during the check", "off", "done", during=bt_off_during_check)
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
