#!/usr/bin/env python3
"""Pre-store the board's BLE pairing PIN, the last step of `make flash` (firmware/bl616_common/host.mk): the device ID from
the board's USB serial number, the PIN derived here with the key in .secrets/pairing_secret (the flashing host is the
manufacturing station; the presence server derives it with the same key for the app), written through the console
(`provision pin` on the product, `pt_ble pin` on the test firmware), then a reboot so the board advertises with it. The
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
from board import BL_PORT, bl, store_open   # noqa: E402

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
store_open()            # the setup test bed opens easyflash only with lp_boot (board.store_open, 2026-11-12)
product = bool(answer("provision", r"provision: pairing PIN (stored|none)"))
cmd = "provision" if product else "pt_ble creds"
if not product and "pairing PIN" not in bl(cmd, 3, r"easyflash init failed|pairing PIN"):
    # the setup test bed: easyflash comes up with its start (2026-11-03). Any answer but a PIN state starts it: the
    # flash's boot messages hid "easyflash init failed" on 2026-11-12, lp_boot was skipped and the store failed
    bl("lp_boot", 30, r"lp: boot done")
state = answer(cmd, r"pairing PIN (stored|none)")
if not state:
    sys.exit("provision_pin: the firmware reports no pairing PIN state (built before the PIN change?)")
if "pairing PIN stored" in state and "--force" not in sys.argv:
    print(f"provision_pin: {dev} holds a pairing PIN already")
    sys.exit(0)
ok = answer(f"{'provision' if product else 'pt_ble'} pin {pin_for(dev, key)}", r"pairing PIN stored|pin store failed")
if "pairing PIN stored" not in ok:
    # the board's own answer (never the PIN: the command line is not echoed here), for the cause
    last = bl(f"{'provision' if product else 'pt_ble creds'}", 3, r"pairing PIN[^\n]*\n")
    tail = " | ".join(l.strip() for l in re.sub(r"\x1b\[[0-9;]*m", "", last).splitlines()[-4:] if l.strip())
    sys.exit(f"provision_pin: storing the PIN on {dev} failed (state now: {tail})")
bl("reboot", 1)
print(f"provision_pin: {dev}: pairing PIN stored, rebooted")
