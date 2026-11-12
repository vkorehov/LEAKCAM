#!/usr/bin/env python3
"""test003 cases: the K230 ROM on USB (29f1:0230) after BOOT1 high + power-on, every way the bench gets there.
2026-11-11, ~1 h without the ROM; the causes found (tools/power-bringup/k230_usb_boot.py docstring):
  1. k230_usb_boot.py read the test firmware's "k230: command not found." as the product refusing and never sent
     pt_* (fixed: refused()); the `script` case below is its regression check.
  2. USB-C 1 half plugged in: no USB event in the VM at all (not automatable; k230_usb_boot.py's path_check() now
     tells it from the ROM side by the NAND image's CDC device).
Ruled out by these cases (12 of 12 in 3.6 s after the reseat): the SDIO pads (parked or as pins_init() left them), a
NAND boot just before, a BL616 reboot just before.
Each try on the BL616 test firmware (bl616_powertest): pt_off 2 s, the case's preparation, the USB boot, up to 15 s for
the ROM, then pt_off + BOOT1 low.

    tests/test003-k230-rails-and-usb-boot/usb_boot_cases.py [n]      default 3 tries per case

Cases:
  plain        pt_boot1 high, pt_on, pt_run (pads as pins_init() left them)
  park         pt_sdu off + pt_sdio park first
  after_nand   a NAND boot to msh first (the K230 ran its image, its USB device up), then pt_off and the try
  after_reboot the BL616 rebooted first (its pins as at power-on), then the try
  script       tools/power-bringup/k230_usb_boot.py on, as flash_nand.sh calls it (cause 1)
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "power-bringup"))
from board import bl, cmd, k230, log   # noqa: E402
import k230_usb_boot                    # noqa: E402

ROM = "29f1:0230"
N = int(sys.argv[1]) if len(sys.argv) > 1 else 3


def rom_up():
    return ROM in subprocess.run(["lsusb"], capture_output=True, text=True).stdout


def off():
    bl("pt_off", 2)
    bl("pt_boot1 low", 0.5)


def prep(case):
    if case == "park":
        bl("pt_sdu off", 0.5)
        bl("pt_sdio park", 0.5)
    elif case == "after_nand":
        bl("pt_on", 1.5)
        fd = k230()
        bl("pt_run", 0.1)
        cmd(fd, None, 10, r"msh />")
        off()
    elif case == "after_reboot":
        bl("reboot", 1)
        time.sleep(6)               # the BL616 back on USB (board.bl() reopens the port)


def try_once(case):
    off()
    if rom_up():
        sys.exit("the ROM is on USB before the try: stop")
    prep(case)
    t0 = time.time()
    if case == "script":
        ok = k230_usb_boot.on()
        dt = time.time() - t0
        off()
        return dt if ok else None
    bl("pt_boot1 high", 0.5)
    bl("pt_on", 1.5)
    bl("pt_run", 0.1)
    t0 = time.time()
    while time.time() - t0 < 15:
        if rom_up():
            dt = time.time() - t0
            off()
            return dt
        time.sleep(0.25)
    off()
    return None


results = {}
for case in ("plain", "park", "after_nand", "after_reboot", "script"):
    for i in range(N):
        dt = try_once(case)
        results.setdefault(case, []).append(dt)
        log(f"{case} {i + 1}/{N}: {'ROM up in %.1f s' % dt if dt is not None else 'NO ROM in 15 s'}")
log("summary: " + "; ".join(f"{c} {sum(d is not None for d in r)}/{len(r)}" for c, r in results.items()))
