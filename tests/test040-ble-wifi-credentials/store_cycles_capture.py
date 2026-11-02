#!/usr/bin/env python3
"""test040 step 9 with the BL616 console captured: connect / store unchecked / disconnect cycles, each one's STATUS
notifications and stored network logged next to the board's own lines (2026-11-01: 4 of 20 cycles failed after the
STATUS attempt-id change, and the final store too).
    tests/test040-ble-wifi-credentials/store_cycles_capture.py [N]     log: store_cycles_capture.log next to this file
"""
import os
import re
import select
import sys
import threading
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import REPO   # noqa: E402,F401  (puts tools/power-bringup on the path)
from board import BL_PORT, openp   # noqa: E402
from phone import op, want   # noqa: E402

N = int(sys.argv[1]) if len(sys.argv) > 1 else 20
LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "store_cycles_capture.log")
out = open(LOG, "w", buffering=1)
t0 = time.time()
stop = False
lines_seen = []                  # the board's lines, for the stored-network reads (one reader owns the port)


def note(text):
    line = f"{time.time() - t0:9.3f} {text}"
    out.write(line + "\n")
    print(line, flush=True)


def reader(fd):
    buf = b""
    while not stop:
        if select.select([fd], [], [], 0.2)[0]:
            try:
                buf += os.read(fd, 65536)
            except OSError:
                note("BOARD console lost")
                return
            *lines, buf = buf.split(b"\n")
            for line in lines:
                s = line.decode(errors="replace").strip()
                if s and "[FU]" not in s and "status event sent" not in s:
                    out.write(f"{time.time() - t0:9.3f}   | {s}\n")
                    lines_seen.append(re.sub(r"\x1b\[[0-9;]*m", "", s))


def board_creds():
    """as phone.creds(), read through the captured console: (ssid len, crc), (psk len, crc)"""
    n = len(lines_seen)
    os.write(fd, b"pt_ble creds\r\n")
    time.sleep(1)
    text = "\n".join(lines_seen[n:])
    got = []
    for key in ("wifi_ssid", "wifi_psk"):
        m = re.search(key + r" len (\d+) crc ([0-9a-f]{8})", text)
        got.append((int(m.group(1)), int(m.group(2), 16)) if m else None)
    return tuple(got)


def hexs(s):
    return s.encode().hex()


fd = openp(BL_PORT)
threading.Thread(target=reader, args=(fd,), daemon=True).start()
os.write(fd, b"pt_ble on\r\n")
time.sleep(5)
addr = op("scan", secs=15).get("address")
if not addr:
    sys.exit("no LEAKCAM in the scan")
fails = 0
for i in range(N):
    s_, p_ = (f"t040-rep{i}", f"repeat-{i:04d}")
    res = op("session", address=addr, pair="accept", writes=f"ssid:{hexs(s_)},psk:{hexs(p_)},commit:02", secs=10)
    st = [x["state"] for x in res.get("states", [])]
    w = [(x["char"], x["status"]) for x in res.get("writes", [])]
    got = board_creds()
    ok = st == [0x04] and got == want(s_, p_)
    fails += not ok
    note(f"cycle {i + 1}: {'ok' if ok else 'FAIL'} states {st} writes {w} error {res.get('error')} "
         f"stored {'this' if got == want(s_, p_) else got}")
note(f"end: {fails}/{N} failed")
stop = True
time.sleep(0.5)
