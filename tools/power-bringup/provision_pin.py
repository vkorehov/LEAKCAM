#!/usr/bin/env python3
"""Pre-store the board's BLE pairing PIN, the last step of `make flash` (firmware/bl616/host.mk): the device ID from
the board's USB serial number, the PIN derived here with the key in .secrets/pairing_secret (the flashing host is the
manufacturing station; the presence server derives it with the same key for the app), written through the console
(`prov pin` on the product, `pt_ble pin` on the test firmware), then a reboot so the board advertises with it. The
PIN is never printed; a board that already holds a PIN is left alone (--force writes it again).
    tools/power-bringup/provision_pin.py [--force]
"""
import hashlib
import hmac
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from board import BL_PORT, bl   # noqa: E402

KEY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".secrets", "pairing_secret")


def pin_for(dev_id, key):
    """HMAC-SHA256(key, the 6 ID bytes), first 4 bytes big-endian mod 10^6 (server/main.go pin())"""
    h = hmac.new(key, bytes.fromhex(dev_id), hashlib.sha256).digest()
    return "%06d" % (int.from_bytes(h[:4], "big") % 1000000)


def answer(cmd, pattern):
    """a console command's own answer; once more when the Wi-Fi logs drowned it (presence check-ins)"""
    for _ in range(3):
        out = bl(cmd, 3, pattern)
        if re.search(pattern, out):
            return out
    return ""


key = bytes.fromhex(open(KEY).read().strip())
if len(key) != 32:
    sys.exit(f"provision_pin: {KEY} must hold 64 hex digits")
for _ in range(30):                              # the console comes back after the flash's reset
    if os.path.exists(BL_PORT):
        break
    time.sleep(1)
usb = subprocess.run(["udevadm", "info", "-q", "property", BL_PORT], capture_output=True, text=True).stdout
m = re.search(r"ID_SERIAL_SHORT=([0-9A-F]{12})\b", usb)
if not m:
    sys.exit("provision_pin: no device ID in the USB serial number (a firmware before use case 2?)")
dev = m.group(1)
product = bool(answer("prov", r"prov: pairing PIN (stored|none)"))
cmd = "prov" if product else "pt_ble creds"
state = answer(cmd, r"pairing PIN (stored|none)")
if not state:
    sys.exit("provision_pin: the firmware reports no pairing PIN state (built before the PIN change?)")
if "pairing PIN stored" in state and "--force" not in sys.argv:
    print(f"provision_pin: {dev} holds a pairing PIN already")
    sys.exit(0)
ok = answer(f"{'prov' if product else 'pt_ble'} pin {pin_for(dev, key)}", r"pairing PIN stored|pin store failed")
if "pairing PIN stored" not in ok:
    sys.exit(f"provision_pin: storing the PIN on {dev} failed")
bl("reboot", 1)
print(f"provision_pin: {dev}: pairing PIN stored, rebooted")
