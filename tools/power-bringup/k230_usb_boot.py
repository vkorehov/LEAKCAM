#!/usr/bin/env python3
"""The K230 into its ROM's USB boot for flashing (on), or off again (off), through whichever BL616 firmware runs:
the product firmwares' `k230 usbboot | off` (bl616_common/k230_power.c, user 2026-11-05: no test firmware needed to
flash), else the test firmware's pt_boot1/pt_on/pt_run. `on` returns once the ROM is on USB (29f1:0230), exit 1 if not.
    tools/power-bringup/k230_usb_boot.py on|off
"""
import subprocess
import sys
import time

from board import bl

ROM = "29f1:0230"


def rom_up():
    return ROM in subprocess.run(["lsusb"], capture_output=True, text=True).stdout


def on():
    if rom_up():
        return True
    out = bl("k230 usbboot", 2, r"k230: [^\n]*\n")
    if "k230: on already" in out:               # a setup session runs (no network stored): end it, then flash
        bl("k230 off", 1.5)
        out = bl("k230 usbboot", 2, r"k230: [^\n]*\n")
    if "k230: USB boot" not in out:
        if "k230:" in out:                      # the product firmware refused: say why
            print(out.strip().splitlines()[-1])
            return False
        for c, s in (("pt_off", 1.5), ("pt_boot1 high", 0.5), ("pt_on", 2), ("pt_run", 1)):   # the test firmware
            bl(c, s)
    t = time.time()
    while time.time() - t < 15:
        if rom_up():
            return True
        time.sleep(0.5)
    return False


def off():
    if "k230: off" not in bl("k230 off", 2, r"k230: off[^\n]*\n"):
        bl("pt_off", 1.5)
        bl("pt_boot1 low", 0.5)


if __name__ == "__main__":
    if sys.argv[1:] == ["on"]:
        ok = on()
        print(f"k230_usb_boot: {'the K230 ROM is on USB (' + ROM + ')' if ok else 'the K230 ROM did not show up on USB'}")
        sys.exit(0 if ok else 1)
    if sys.argv[1:] == ["off"]:
        off()
        sys.exit(0)
    sys.exit(__doc__)
