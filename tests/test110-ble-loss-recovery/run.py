#!/usr/bin/env python3
"""test110: losses during a Bluetooth setup on the product pair and the real app (user 2026-11-08: "test 1xx should
test against product fw and apps"): the faults come from outside, as they do for a person - the phone's Bluetooth off
for 4 s before the save and during the Wi-Fi check, the board reset during the check. Each case: a named result
within 35 s, the app and the board agree (Done only with the network stored, a failure only without it), and a case
that did not end in Done is followed by a clean setup, Done (recovery). The board's network is erased before each case
(in the setup firmware it stays there), so "stored" can be read. The old test-bed hooks (lost notifications, the
link dropped at a chosen step) have no product equivalent: their 2026-11-03 results are in HISTORY.md. No hands.
    tests/test110-ble-loss-recovery/run.py      (the product pair, USB-C 1 in, the phone on adb)
"""
import fcntl
import os
import sys
import threading
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import log, wifi_credentials   # noqa: E402
from phone import erase, adb, creds, firmware, only_boards, op, ready, to_firmware, want   # noqa: E402

NAMED_S = 35               # a failure named well before the app's 60 s step timeout
checks, times = [], {}

_lock = open(__file__)
try:
    fcntl.flock(_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except OSError:
    sys.exit("test110: another run.py is running")


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def applog():
    return adb("logcat", "-d", "-s", "LeakcamSetup:I", secs=15)


def bt(on):
    adb("shell", "cmd", "bluetooth_manager", "enable" if on else "disable", secs=15)


def fresh():
    """a new board: the setup firmware, nothing stored, off the phone's list"""
    if firmware() != "setup":
        to_firmware("setup", "switch_to_setup lost")
    erase()
    only_boards()


def setup(fault=None, when=None):
    """one setup through the app; fault() runs once the app logged `when`. (last help text, seconds, stored)"""
    fresh()
    adb("logcat", "-c")
    stop = []

    def watch():
        while not stop:
            if when in applog():
                log(f"fault at '{when}'")
                fault()
                return
            time.sleep(0.2)
    th = threading.Thread(target=watch, daemon=True) if fault else None
    if th:
        th.start()
    res = op("app", ssid=ssid.encode().hex(), psk=psk.encode().hex(), anyway="", secs=60)
    stop.append(1)
    if th:
        th.join(20)
    bt(True)
    helps = res.get("help", [])
    last = helps[-1]["help"] if helps else res.get("error", "no help text")
    time.sleep(2)
    stored = creds() == want(ssid, psk)
    return last.replace(ssid, "<network>"), res.get("ms_end", 0) / 1000, stored


def case(name, fault, when):
    last, secs, stored = setup(fault, when)
    done = last.startswith("Done.")
    # "cannot tell whether the network was stored" (err_unconfirmed: the link lost after the commit, the board then
    # in the full firmware with no BLE to read STATUS from) is honest either way; Done must mean stored, a failure
    # must mean nothing stored (2026-11-08)
    unsure = "cannot tell whether" in last
    claim = "Done" if done else "cannot tell" if unsure else "failed"
    check(f"{name}: a result named within {NAMED_S} s", secs < NAMED_S, f"{secs:.1f} s: {last[:70]}")
    check(f"{name}: the app and the board agree", unsure or done == stored,
          f"app {claim}, board {'stored' if stored else 'nothing stored'}")
    times[name] = secs
    if not done:
        last, secs, stored = setup()
        check(f"{name}: then a clean setup, Done", last.startswith("Done.") and stored, f"{secs:.1f} s")


def bt_off_4s():
    bt(False)
    time.sleep(4)
    bt(True)


r = Run(__file__)
why = ready(r.notes)
if why:
    r.finish(f"not run: {why}", False, switch_off=False)
    sys.exit(1)
ssid, psk = wifi_credentials()
try:
    last, secs, stored = setup()
    check("baseline: Done, the network stored", last.startswith("Done.") and stored, f"{secs:.1f} s")
    times["baseline"] = secs
    case("phone Bluetooth off 4 s before the save", bt_off_4s, "Paired. Sending the Wi-Fi settings")
    case("phone Bluetooth off 4 s during the check", bt_off_4s, "now joining")
    case("the board reset during the check", lambda: bl("reboot", 0.5), "now joining")
    r.sample("after the cases")
    r.set("speed", "app opened -> result: " + ", ".join(f"{k} {v:.1f} s" for k, v in times.items()) + f" ({r.date})")
finally:
    bt(True)
    if firmware() != "full":                 # the bench as found: the bench network, the full firmware
        last, secs, stored = setup()
        to_firmware("full", secs=30)
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
