#!/usr/bin/env python3
"""test040 hand steps (the current metrics): the user only moves cables or reads the meter; the
board's own USB-C 1 power reading (`pt_ble`; the console stays up on PR1) marks the unplug and the plug, the phone
(BleOp over adb, as run.py) does the BLE side.

  hand_steps.py meter       test firmware, cell full: 60 s holds for the USB-C 1 meter - BLE off, advertising,
                            connected, a Wi-Fi check
  (the USB power gate it also checked is gone since 2026-11-01: the PIN proves the phone; the battery behaviour,
  advertising windows while no network is stored, is product logic: tests/test116-ble-only-setup/run.py)
"""
import os
import re
import sys
import termios
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup"))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from board import bl, log, wifi_credentials   # noqa: E402
from phone import op   # noqa: E402

def console_back(secs=60):
    """the console (PR1) answers again after a watchdog reset; the old port name can outlive the device a moment"""
    t0 = time.time()
    while time.time() - t0 < secs:
        try:
            if "pt_ble" in bl("help", 2):
                return
        except (OSError, termios.error):
            pass
        time.sleep(0.5)
    sys.exit("the console did not come back")


def scan(secs):
    res = op("scan", secs=secs)
    return res if res.get("found") else None


def meter():
    """the USB-C 1 VBUS meter with the cell full (charging read 443 mA): one 60 s hold per state, the user reads"""
    ssid, psk = wifi_credentials()
    bl("pt_hang", 1)                                 # watchdog reset: BLE off
    time.sleep(4)
    console_back()
    log("METER 1: BLE off, idle (60 s)")
    time.sleep(60)
    bl("pt_ble on", 5, r"ble: ready as|already on")
    log("METER 2: BLE advertising (60 s)")
    time.sleep(60)
    r = scan(20)
    if not r:
        log("CHECK FAIL the phone does not see the board")
        return
    op("unpair", address=r["address"])
    log("METER 3: phone connected and paired, idle link (about 60 s)")
    op("session", address=r["address"], pair="accept", writes="", secs=60)
    log("METER 4: Wi-Fi check running (join, DHCP, internet; about 10 s - read the peak)")
    res = op("session", address=r["address"], pair="accept",
             writes=f"ssid:{ssid.encode().hex()},psk:{psk.encode().hex()},commit:01", secs=90)
    log("CHECK", "pass" if [x["state"] for x in res.get("states", [])][-1:] == [4] else "FAIL", "check SAVED",
        [x["state"] for x in res.get("states", [])])
    op("unpair", address=r["address"])
    log("METER done")


if __name__ == "__main__":
    {"meter": meter}[sys.argv[1]]()
