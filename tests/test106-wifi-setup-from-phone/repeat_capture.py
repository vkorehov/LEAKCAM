#!/usr/bin/env python3
"""test106 repeat with the BL616 console captured: the app's setup again and again while one reader holds the BL616
console and timestamps every line (the check's own "check of ...: state" line, BLE events); stops at the first setup
that does not end in Done. run.py opens the console only per command, so a stall's board lines were lost
(2026-11-01: one of 4 repeats "did not answer" after "Getting an address", 67.6 s).
    tests/test106-wifi-setup-from-phone/repeat_capture.py [N]      log: repeat_capture.log next to this file
"""
import os
import select
import sys
import threading
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import REPO   # noqa: E402,F401  (puts tools/power-bringup on the path)
from board import BL_PORT, openp, wifi_credentials   # noqa: E402
from phone import op, ready   # noqa: E402

N = int(sys.argv[1]) if len(sys.argv) > 1 else 20
LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "repeat_capture.log")
out = open(LOG, "w", buffering=1)
t0 = time.time()
stop = False


def note(text):
    out.write(f"{time.time() - t0:9.3f} {text}\n")
    print(f"{time.time() - t0:9.3f} {text}", flush=True)


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
                if s:
                    out.write(f"{time.time() - t0:9.3f}   | {s}\n")


ssid, psk = wifi_credentials()
why = ready([])
if why:
    sys.exit(why)
fd = openp(BL_PORT)
threading.Thread(target=reader, args=(fd,), daemon=True).start()
os.write(fd, b"crash\r\nmem\r\n")
fails = 0
for i in range(1, N + 1):
    res = op("app", ssid=ssid.encode().hex(), psk=psk.encode().hex(), anyway="", secs=150)
    helps = res.get("help", [])
    last = helps[-1]["help"] if helps else res.get("error", "no help text")
    note(f"setup {i}: {res.get('ms_end', 0) / 1000:.1f} s " + " | ".join(h["help"][:60].replace("\n", " ")
                                                                       for h in helps[-3:]))
    os.write(fd, b"mem\r\n")
    time.sleep(1)
    if not last.startswith("Done."):
        fails += 1
        note(f"STALL at setup {i}")
        time.sleep(5)
        os.write(fd, b"crash\r\nmem\r\n")
        time.sleep(2)
        break
note(f"end: {i} setups, {fails} failed")
stop = True
time.sleep(0.5)
