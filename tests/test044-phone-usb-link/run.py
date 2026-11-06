#!/usr/bin/env python3
"""test044: the phone, plugged into USB-C 1 with a USB-C cable (phone = USB host), sees the board's K230 USB device
(1209:ABD1, test image) - the link the app's USB setup will use (DESIGN.md 5.3.4, test113). Hand steps: USB-C 1 from
the bench to the phone, at the end back. The phone's own view only: `dumpsys usb` (host_manager, port status) over
adb on Wi-Fi; 5 K230 starts timed from pt_run to the phone listing the device (no VMware in the path: test043 had
~3.3 s of it).
    tests/test044-phone-usb-link/run.py
"""
import os
import re
import statistics
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import log   # noqa: E402
from phone import adb, hand, ready   # noqa: E402

VID, PID = 0x1209, 0xABD1
STARTS = 5
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def usb():
    """the phone's USB state: (board listed, power_role, data_role)"""
    out = adb("shell", "dumpsys", "usb", secs=15)
    host = out.split("host_manager=", 1)[-1].split("port_manager=", 1)[0]
    listed = f"vendor_id={VID}" in host and f"product_id={PID}" in host
    status = out.split("port_manager=", 1)[-1]
    p = re.search(r"power_role=([\w-]+)", status)
    d = re.search(r"data_role=([\w-]+)", status)
    return listed, p.group(1) if p else "?", d.group(1) if d else "?"


def until(want, secs):
    """seconds until usb()[0] == want, None if not within secs"""
    t0 = time.time()
    while time.time() - t0 < secs:
        if usb()[0] == want:
            return time.time() - t0
        time.sleep(0.05)
    return None


def k230_off():
    bl("pt_off", 2)


def k230_start():
    """K230 off -> on -> run (NAND boot, the test image); the time of pt_run"""
    k230_off()
    time.sleep(1)
    bl("pt_boot1 low")
    bl("pt_on", 2)
    t = time.time()
    bl("pt_run", 0.1)
    return t


r = Run(__file__)
notes = []
why = ready(notes)
if why:
    r.finish(f"not run: {why}", False)
    sys.exit(1)
k230_off()
if hand("USB-C 1: take the cable off the bench, plug the phone in with a USB-C cable", "phone") is None:
    r.finish("not run: no hand step (phone not plugged)", False)
    sys.exit(1)
time.sleep(3)
listed, power, data = usb()
check("K230 off: the phone lists no board device", not listed)
times = []
for i in range(STARTS):
    t = k230_start()
    while not usb()[0] and time.time() - t < 20:
        time.sleep(0.05)
    seen = time.time() - t
    if usb()[0]:
        times.append(seen)
        log(f"start {i + 1}: the phone lists 1209:ABD1 {seen:.2f} s after pt_run")
    else:
        log(f"start {i + 1}: not listed within 20 s")
listed, power, data = usb()
check(f"the phone sees the board's USB device after every K230 start ({STARTS})", len(times) == STARTS,
      f"{len(times)}/{STARTS}; median {statistics.median(times):.2f} s, max {max(times):.2f} s" if times else "never")
check("the phone is the USB host and powers the cable", data == "host" and power == "source",
      f"power_role {power}, data_role {data}")
k230_off()
gone = until(False, 10)
check("K230 off: the device leaves the phone's list", gone is not None, f"{gone:.2f} s" if gone is not None else "still listed")
if times:
    r.set("speed", f"phone sees the K230 USB device {statistics.median(times):.2f} s (median of {len(times)}), "
          f"max {max(times):.2f} s after pt_run; gone {gone:.2f} s after pt_off" if gone is not None else "")
hand("USB-C 1: take the phone off, plug the bench cable back in", "bench")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed)
sys.exit(1 if failed else 0)
