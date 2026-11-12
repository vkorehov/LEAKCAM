#!/usr/bin/env python3
"""Store the board's mTLS credentials (DESIGN.md 5.3.12 design decision, firmware/bl616_common/tls_link.h), a step of
`make flash` after provision_pin.py: the device ID from the board's USB serial number, its certificate made once by
tools/pki/leakcam_pki.py (EC P-256, 20 years, signed by the LEAKCAM CA, kept in .secrets/pki), then the CA certificate,
the board's certificate and its private key written to flash through the console (`provision tls`) in 48-byte hex
chunks, each blob checked by length and CRC-32 on the board before it is stored. Only lengths and CRCs are printed; a
board that holds the same three already is left alone (--force writes them again).
    tools/power-bringup/provision_tls.py [--force]
"""
import os
import re
import subprocess
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from board import BL_PORT, bl, store_open   # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PKI = os.path.join(HERE, "..", "pki", "leakcam_pki.py")
CHUNK = 48                                       # 96 hex digits: the console's line is 120 characters


def answer(cmd, pattern, secs=3):
    """a console command's own answer; again when the Wi-Fi logs drowned it"""
    for _ in range(3):
        out = re.sub(r"\x1b\[[0-9;]*m", "", bl(cmd, secs, pattern))
        if re.search(pattern, out):
            return out
    return ""


for _ in range(30):                              # the console comes back after the flash's reset
    if os.path.exists(BL_PORT):
        break
    time.sleep(1)
usb = subprocess.run(["udevadm", "info", "-q", "property", BL_PORT], capture_output=True, text=True).stdout
m = re.search(r"ID_SERIAL_SHORT=([0-9A-F]{12})\b", usb)
if not m:
    sys.exit("provision_tls: no device ID in the USB serial number")
dev = m.group(1)
subprocess.run([sys.executable, PKI, "device", dev], check=True)
ca, crt, key = subprocess.run([sys.executable, PKI, "der", dev], check=True, capture_output=True,
                              text=True).stdout.split()
blobs = [("ca", open(ca, "rb").read()), ("crt", open(crt, "rb").read()), ("key", open(key, "rb").read())]

store_open()            # the setup test bed opens easyflash only with lp_boot (board.store_open, 2026-11-12)
state = answer("provision", r"provision: tls_key (none|len \d+ crc [0-9a-f]+)|provision: pairing PIN")
if "provision: pairing PIN" not in state:
    print("provision_tls: not a product firmware (no `provision`): skipped")   # the test firmwares (pt_ble)
    sys.exit(0)
if "tls_key" not in state:
    state = answer("provision", r"provision: tls_key (none|len \d+ crc [0-9a-f]+)")
if not state:
    sys.exit("provision_tls: the product firmware reports no TLS credentials state (built before mTLS?)")
have = {n: (int(a), b) for n, a, b in re.findall(r"provision: tls_(ca|crt|key) len (\d+) crc ([0-9a-f]{8})", state)}
want = {n: (len(b), "%08x" % zlib.crc32(b)) for n, b in blobs}
if have == want and "--force" not in sys.argv:
    print(f"provision_tls: {dev} holds its CA, certificate and key already")
    sys.exit(0)
for name, data in blobs:
    if have.get(name) == want[name] and "--force" not in sys.argv:
        continue
    if "begin" not in answer(f"provision tls {name} begin {len(data)}", r"provision: tls \S+ (begin|:)"):
        sys.exit(f"provision_tls: {dev}: {name} not started")
    for i in range(0, len(data), CHUNK):
        part = data[i:i + CHUNK]
        got = answer(f"provision tls {name} {part.hex()}", rf"provision: tls ({name} \d+/\d+|bad chunk|{name}: no begin)")
        if f"{name} {i + len(part)}/{len(data)}" not in got:
            sys.exit(f"provision_tls: {dev}: {name} chunk at {i} not taken")
    done = answer(f"provision tls {name} end {want[name][1]}", r"provision: tls_\S+ len \d+ crc \S+ stored|check failed|"
                  r"store failed")
    if "stored" not in done or "failed" in done:
        sys.exit(f"provision_tls: {dev}: {name} not stored")
    print(f"provision_tls: {dev}: tls_{name} {len(data)} B stored")
bl("reboot", 1)
print(f"provision_tls: {dev}: CA, certificate and key stored, rebooted")
