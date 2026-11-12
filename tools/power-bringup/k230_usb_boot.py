#!/usr/bin/env python3
"""The K230 into its ROM's USB boot for flashing (on), or off again (off), through whichever BL616 firmware runs:
the product firmwares' `k230 usbboot | off` (bl616_common/k230_power.c, user 2026-11-05: no test firmware needed to
flash), else the test firmware's pt_boot1/pt_on/pt_run. `on` returns once the ROM is on USB (29f1:0230), exit 1 if not.
    tools/power-bringup/k230_usb_boot.py on|off

The ROM missing on USB, 2026-11-11 (~1 h lost, test003 usb_boot_cases.py), two causes, both now handled:
  1. This script: the test firmware answers `k230 usbboot` with "k230: command not found.", which the old check read
     as the product firmware refusing ("k230:" in the answer), so it returned in 0.1 s without ever sending the test
     firmware's pt_* sequence. Now only the product's own answers count as a refusal (refused()).
  2. USB-C 1 half plugged in (VBUS and PGOOD present, no data): the VM logged no USB event at all. A NAND boot tells
     this apart from the ROM side: the image's own CDC device (1209:abd1) enumerates on the same path or not. So
     on(): no ROM in 15 s -> path_check() -> one more try, and the message names the side that failed.
After the fix and the reseat: 12 of 12 tries in 3.6 s (plain, SDIO pads parked, after a NAND boot, after a BL616
reboot): the BOOT1 strap and the SDIO pads were never the cause.
"""
import subprocess
import sys
import time

from board import bl

ROM = "29f1:0230"
CDC = "1209:abd1"           # the K230 image's own USB device (CDC ACM on USB0, firmware/k230_board/install.sh)


def rom_up():
    return ROM in subprocess.run(["lsusb"], capture_output=True, text=True).stdout


def wait_usb(dev, secs):
    t = time.time()
    while time.time() - t < secs:
        if dev in subprocess.run(["lsusb"], capture_output=True, text=True).stdout:
            return True
        time.sleep(0.5)
    return False


def path_check():
    """the USB-C 1 path without the ROM: the NAND image's CDC device must enumerate (needs an image on the NAND).
    True = the path and the host's port work (the CDC came, 2026-11-11: ~2 s after the msh prompt); None = not
    possible here: the product firmware has no plain power-on (it starts the K230 by itself on USB power)"""
    off()
    if "K230 reset" not in bl("pt_status", 1):
        return None
    for c, s in (("pt_boot1 low", 0.5), ("pt_on", 1.5), ("pt_run", 1)):
        bl(c, s)
    ok = wait_usb(CDC, 20)
    off()
    return ok


def on():
    if rom_up():
        return True
    if try_on():
        return True
    # no ROM in 15 s: check the path with a NAND boot, then once more (module docstring, 2026-11-11)
    print("k230_usb_boot: no ROM in 15 s; checking USB-C 1 with a NAND boot (the image's CDC device, " + CDC + ")")
    ok = path_check()
    if ok is None:
        print("k230_usb_boot: the path check needs the BL616 test firmware (bl616_powertest); on the product: reseat "
              "USB-C 1, relay box to the bench, then again")
        return False
    if not ok:
        print("k230_usb_boot: the NAND image's USB device did not enumerate either: the USB-C 1 path (plug fully in? "
              "relay box on the bench? host port / VMware) or no image on the NAND - not the ROM")
        return False
    print("k230_usb_boot: USB-C 1 works (CDC enumerated); USB boot once more")
    if try_on():
        return True
    print("k230_usb_boot: the path works but the ROM does not come: BOOT1 strap / ROM side (test003)")
    return False


def refused(out):
    """the product firmware's own refusal ("k230: on already ...", "k230: the 3V3 did not come up"), not the test
    firmware's "k230: command not found." (that one means: send the pt_* sequence; module docstring cause 1)"""
    return "k230:" in out and "command not found" not in out


def try_on():
    out = bl("k230 usbboot", 2, r"k230: [^\n]*\n")
    if "k230: on already" in out:               # a setup session runs (no network stored): end it, then flash
        bl("k230 off", 1.5)
        out = bl("k230 usbboot", 2, r"k230: [^\n]*\n")
    if "k230: USB boot" not in out:
        if refused(out):                        # the product firmware refused: say why
            print(out.strip().splitlines()[-1])
            return False
        for c, s in (("pt_off", 1.5), ("pt_boot1 high", 0.5), ("pt_on", 2), ("pt_run", 1)):   # the test firmware
            bl(c, s)
    return wait_usb(ROM, 15)


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
