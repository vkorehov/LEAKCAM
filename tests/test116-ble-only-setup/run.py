#!/usr/bin/env python3
"""test116: the whole setup over BLE alone, no USB, on the product firmware (USB-C 1 unplugged and plugged back by the user, the cell stays in;
the console stays up on PR1): no USB power gate since 2026-11-01, the PIN proves the phone. A board with no network
advertises on battery 60 s every 5 minutes: the phone finds it within 5 minutes and the app sets it up on battery;
configured, it is silent on battery; plugged in, it advertises again.
    tests/test116-ble-only-setup/run.py
"""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import bl   # noqa: E402  (puts tools/power-bringup on the path)
from board import log, wifi_credentials   # noqa: E402
from phone import op, ready   # noqa: E402

WINDOW_S, PERIOD_S = 60, 300                 # firmware/bl616/provision.c


def usb(present, secs=600):
    """USB-C 1 power as the board sees it (`prov`)"""
    t0 = time.time()
    while time.time() - t0 < secs:
        m = re.search(r"USB power (yes|no)", bl("prov", 2, r"USB power (yes|no)"))
        if m and (m.group(1) == "yes") == present:
            return time.time()
        time.sleep(0.5)
    sys.exit("timed out waiting for USB-C 1 " + ("in" if present else "out"))


def scan(secs):
    res = op("scan", secs=secs)
    return res if res.get("found") else None


notes = []
why = ready(notes)
if why:
    sys.exit(f"phone not ready: {why}")
if "prov: USB power" not in bl("prov", 2, r"prov: USB power"):
    sys.exit("the product firmware is needed (firmware/bl616, make flash)")
ssid, psk = wifi_credentials()
usb(True)
bl("prov erase", 2)                          # no network: the board advertises on battery in its windows
log("ACTION: unplug USB-C 1 now (the cell stays in)")
t_unplug = usb(False)
r = scan(PERIOD_S + WINDOW_S)
log("CHECK", "pass" if r else "FAIL", "no network, on battery: found within one period",
    r and f"{time.time() - t_unplug:.0f} s after the unplug")
res = op("app", ssid=ssid.encode().hex(), psk=psk.encode().hex(), secs=150)
helps = [h["help"] for h in res.get("help", [])]
log("CHECK", "pass" if helps and helps[-1].startswith("Done.") else "FAIL", "set up from the app on battery (PIN)",
    helps[-1][:80] if helps else res.get("error"))
log("CHECK", "pass" if "len" in bl("prov", 2, r"wifi_psk[^\n]*\n") else "FAIL", "network stored on battery")
r = scan(PERIOD_S + WINDOW_S)
log("CHECK", "FAIL" if r else "pass", "configured, on battery: silent for a whole period")
log("ACTION: plug USB-C 1 back in")
t_plug = usb(True)
r = scan(30)
log("CHECK", "pass" if r else "FAIL", "advertises again on USB power",
    r and "%.1f s after the plug" % (time.time() - t_plug))
log(f"test116 done, {time.time() - t_unplug:.0f} s from the unplug")
