#!/usr/bin/env python3
"""test040 hand steps (README Steps 1, 2 and the current metrics): the user only moves cables or reads the meter; the
board's own USB-C 1 power reading (`pt_ble`; the console stays up on PR1) marks the unplug and the plug, the phone
(BleOp over adb, as run.py) does the BLE side.

  hand_steps.py meter       test firmware, cell full: 60 s holds for the USB-C 1 meter - BLE off, advertising,
                            connected, a Wi-Fi check
  hand_steps.py usb-gate    test firmware: advertising, a held connection dropped on unplug, nothing on battery
                            (no advertising, no connection, so no pairing), advertising again after the plug
                            (BLE restarted by command: the test firmware does not restart it by itself)
"""
import os
import re
import sys
import termios
import threading
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


def usb_wait(present, secs=600):
    """USB-C 1 power as the board sees it (`pt_ble`): the console stays up on PR1 while USB-C 1 is out"""
    t0 = time.time()
    while time.time() - t0 < secs:
        m = re.search(r"USB power (yes|no)", bl("pt_ble", 1, r"USB power (yes|no)"))
        if m and (m.group(1) == "yes") == present:
            return time.time()
        time.sleep(0.2)
    sys.exit("timed out waiting for USB-C 1 " + ("in" if present else "out"))


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
    bl("pt_ble on", 5, r"advertising as|already on")
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


def ble_fresh():
    """watchdog reset, then BLE on: the test firmware does not restart BLE by itself after an unplug"""
    bl("pt_hang", 1)
    time.sleep(4)
    console_back()
    bl("pt_ble on", 5, r"advertising as|already on")


def usb_gate():
    usb_wait(True)
    ble_fresh()
    r = scan(20)
    log("CHECK", "pass" if r else "FAIL", "advertises on USB power", r and f"{r['name']} rssi {r['rssi']}")
    if not r:
        return
    addr = r["address"]
    op("unpair", address=addr)
    held = {}
    t = threading.Thread(target=lambda: held.update(op("session", address=addr, pair="accept", writes="", secs=120),
                                                    t_end=time.time()))
    t.start()
    time.sleep(20)                                   # connected, paired, notifications on: the link is held
    log("ACTION: unplug USB-C 1 now (the cell stays in)")
    t_unplug = usb_wait(False)
    t.join(150)
    if "disconnected_ms" in held:
        log("CHECK pass held connection dropped %.1f s after the unplug" % (held["t_end"] - t_unplug))
    else:
        log("CHECK FAIL held connection not dropped:", {k: held.get(k) for k in ("error", "pair", "bond")})
    r = scan(15)
    log("CHECK", "FAIL" if r else "pass", "no advertising on battery (15 s scan)")
    res = op("session", address=addr, pair="accept", writes="ssid:" + "t040-battery".encode().hex(), secs=5)
    log("CHECK", "pass" if res.get("error") == "no connection" else "FAIL",
        "no connection on battery, so no pairing and no write", res.get("error") or res.get("writes"))
    log("ACTION: plug USB-C 1 back in")
    t_plug = usb_wait(True)
    ble_fresh()
    r = scan(30)
    log("CHECK", "pass" if r else "FAIL", "advertises again after the plug",
        r and "seen %.1f s after the plug" % (time.time() - t_plug))
    op("unpair", address=addr)


{"meter": meter, "usb-gate": usb_gate}[sys.argv[1]]()
