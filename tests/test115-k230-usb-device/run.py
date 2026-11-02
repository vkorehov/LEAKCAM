#!/usr/bin/env python3
"""test115 automated (K230 test image with the USB device, tests/firmware/test_install.sh): the K230 boots with USB-C 1
cabled to the bench (this VM sees it); its CherryUSB CDC ACM (1209:ABD1, /dev/leakcam-k230usb by udev) enumerates;
`usb_echo` echoes it back: 200 round trips of 16 bytes (latency) and 1 MB checked byte for byte (throughput). The
bench stands in for the phone: nothing to recable (user, 2026-11-01).
    tests/test115-k230-usb-device/run.py
"""
import os
import select
import statistics
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run   # noqa: E402
from board import log, openp   # noqa: E402

PORT = "/dev/leakcam-k230usb"
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def read_exact(fd, n, deadline):
    got = b""
    while len(got) < n and time.time() < deadline:
        if select.select([fd], [], [], 0.05)[0]:
            try:
                got += os.read(fd, n - len(got))
            except BlockingIOError:
                pass
    return got


r = Run(__file__)
t_boot = time.time()
r.boot()
t0 = time.time()
while time.time() - t0 < 20 and not os.path.exists(PORT):
    time.sleep(0.2)
enum_s = time.time() - t_boot
check("the K230's USB device enumerates on the bench (1209:ABD1)", os.path.exists(PORT), f"{enum_s:.1f} s after the boot")
if not os.path.exists(PORT):
    r.obstacle("no /dev/leakcam-k230usb: the image without the USB device (test_install.sh), or USB0 not up")
    r.finish("not run: the K230 USB device did not enumerate.", False)
    sys.exit(1)
r.k("usb_echo 60", 2, r"echoing on ttyGS0|not available")
time.sleep(0.5)
fd = openp(PORT)
lat, bad = [], 0
for i in range(200):
    tx = bytes((i * 7 + k) & 255 for k in range(16))
    t1 = time.perf_counter()
    os.write(fd, tx)
    rx = read_exact(fd, 16, time.time() + 0.5)
    if rx == tx:
        lat.append((time.perf_counter() - t1) * 1e6)
    else:
        bad += 1
check("200 round trips of 16 bytes, all back", bad == 0, f"{len(lat)} ok, {bad} bad")
total, chunk, sent, recv, mism = 1 << 20, 4096, 0, 0, 0
pattern = bytes((k * 13 + 7) & 255 for k in range(chunk))
t1 = time.perf_counter()
deadline = time.time() + 30
while recv < total and time.time() < deadline:
    if sent < total and sent - recv < 4 * chunk:
        sent += os.write(fd, pattern[: min(chunk, total - sent)])
    got = read_exact(fd, min(chunk, total - recv), time.time() + 0.01)
    for k, b in enumerate(got):
        if b != pattern[(recv + k) % chunk]:
            mism += 1
    recv += len(got)
secs = time.perf_counter() - t1
rate = recv / secs if secs else 0
check("1 MB echoed, every byte checked", recv == total and mism == 0, f"{recv} B, {mism} wrong, {rate / 1024:.0f} KB/s")
os.close(fd)
out = r.k(None, 10, r"bytes echoed")
if lat:
    r.set("speed", f"enumeration {enum_s:.1f} s after the boot; 16 B round trip median {statistics.median(lat):.0f} us, "
          f"max {max(lat):.0f} us; echo throughput {rate / 1024:.0f} KB/s both ways (1 MB checked)")
r.sample("after the echo")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed)
sys.exit(1 if failed else 0)
