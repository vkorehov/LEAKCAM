#!/usr/bin/env python3
"""test106 step 7 by hand (USB-C 1 unplugged and plugged back by the user, the cell stays in): on battery the Setup
app's Find lists no board and a stored bond cannot connect; after the plug the board is found again. The board-side
checks are test040's usb-gate; this one drives the app itself (BleOp op app, Find only: the board is never reached).
    tests/test106-wifi-setup-from-phone/hand_step7.py
"""
import glob
import os
import sys
import time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(glob.glob(os.path.join(HERE, "..", "test040-*", "hand_steps.py"))[0]))
from hand_steps import ble_fresh, scan, usb_wait   # noqa: E402  (puts tools/ on the path)
from board import log   # noqa: E402
from phone import op, ready   # noqa: E402

DUMMY = "t106-battery".encode().hex()      # never the real network: on battery nothing may reach the board

notes = []
why = ready(notes)
if why:
    sys.exit(f"phone not ready: {why}")
usb_wait(True)
ble_fresh()
r = scan(20)
log("CHECK", "pass" if r else "FAIL", "advertises on USB power", r and f"{r['name']} rssi {r['rssi']}")
if not r:
    sys.exit(1)
addr = r["address"]
op("unpair", address=addr)
res = op("session", address=addr, pair="accept", writes="", secs=40)
log("CHECK", "pass" if res.get("bond") == 12 else "FAIL", "bond stored on the phone before the unplug (BOND_BONDED 12)",
    {k: res.get(k) for k in ("error", "pair", "bond")})
log("ACTION: unplug USB-C 1 now (the cell stays in)")
t_unplug = usb_wait(False)
time.sleep(2)
res = op("app", ssid=DUMMY, psk=DUMMY, secs=10)
helps = [h["help"] for h in res.get("help", [])]
log("CHECK", "pass" if res.get("found") == 0 else "FAIL", "on battery the app lists no board (Find, 15 s)",
    f"found {res.get('found')}; help: {' | '.join(h[:80].replace(chr(10), ' ') for h in helps[-2:])}")
res = op("session", address=addr, pair="accept", writes="ssid:" + DUMMY, secs=5)
log("CHECK", "pass" if res.get("error") == "no connection" else "FAIL",
    "on battery the stored bond cannot connect", res.get("error") or res.get("writes"))
log("ACTION: plug USB-C 1 back in")
t_plug = usb_wait(True)
ble_fresh()
r = scan(30)
log("CHECK", "pass" if r else "FAIL", "found again after the plug", r and "%.1f s after the plug" % (time.time() - t_plug))
op("unpair", address=addr)
log(f"step 7 done, {time.time() - t_unplug:.0f} s from the unplug")
