#!/usr/bin/env python3
"""test040 PDS A/B: does the BLE controller's PDS reset (SDK weak hook btblecontroller_software_pds_reset, GLB
AHB MCU software reset of PDS) drop the BL616's USB device? 5 cycles of watchdog reset -> `pt_ble on`; per cycle:
a kernel "USB disconnect" for the board after `pt_ble on`, the console still answering, BLE advertising.

  pds_ab.py <label>     label: which build is on the board (e.g. "sdk-reset" or "no-reset")
"""
import os
import re
import subprocess
import sys
import termios
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup"))
from board import bl, log   # noqa: E402


def console_back(secs=60):
    t0 = time.time()
    while time.time() - t0 < secs:
        try:
            if "pt_ble" in bl("help", 2):
                return True
        except (OSError, termios.error):
            pass
        time.sleep(0.5)
    return False


def usb_disconnects(since):
    out = subprocess.run(["journalctl", "-k", "--since", since, "--no-pager", "-o", "cat"],
                         capture_output=True, text=True).stdout
    return len(re.findall(r"USB disconnect", out))


ok = 0
for i in range(1, 6):
    bl("pt_hang", 1)
    time.sleep(4)
    if not console_back():
        log("CHECK FAIL cycle", i, "console did not come back after the watchdog reset")
        continue
    time.sleep(2)
    since = time.strftime("%Y-%m-%d %H:%M:%S")
    try:
        adv = "ble: ready as" in bl("pt_ble on", 5, r"ble: ready as|already on")
    except (OSError, termios.error):
        adv = False
    time.sleep(5)
    gone = usb_disconnects(since)
    try:
        alive = "pt_ble" in bl("help", 2)
    except (OSError, termios.error):
        alive = False
    good = adv and alive and gone == 0
    ok += good
    log("CHECK", "pass" if good else "FAIL", f"cycle {i} [{sys.argv[1]}]: advertising {adv}, USB disconnects {gone}, "
        f"console {'answers' if alive else 'gone'}")
log(f"{sys.argv[1]}: {ok} of 5 cycles with USB kept")
