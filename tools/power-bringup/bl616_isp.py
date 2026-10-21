#!/usr/bin/env python3
"""Put the BL616 into its ROM download mode over the USB console, no BOOT link or EN touch.

    bl616_isp.py              -> prints the ROM's port (/dev/ttyACMn) once it is there
    bl616_isp.py --wait-app   -> after flashing: waits for the firmware's console, prints its port

Both firmwares (test and product) are built for the SDK board bl616dk, whose shell has `reboot uart`:
it sets the HBN user-boot field to 1 and does a power-on reset, and the ROM then starts as if BOOT were
high. Their console is the SDK's USB CDC (ffff:ffff); the ROM enumerates as Bouffalo CDC (349b:6160).
A firmware that never reaches its console cannot be switched this way: BOOT + EN by hand
(README.md section 2). Under VMware every re-enumeration needs "connect to Linux" on the Mac.
"""
import argparse
import glob
import os
import subprocess
import sys
import termios
import time

ROM = ("349b", "6160")
APP = ("ffff", "ffff")


def ports():
    """{(vid, pid): /dev/ttyACMn} from sysfs."""
    found = {}
    for tty in sorted(glob.glob("/sys/class/tty/ttyACM*")):
        dev = os.path.realpath(os.path.join(tty, "device", ".."))
        try:
            ids = tuple(open(os.path.join(dev, f)).read().strip() for f in ("idVendor", "idProduct"))
        except OSError:
            continue
        found[ids] = "/dev/" + os.path.basename(tty)
    return found


def wait_for(ids, what, timeout):
    """A new ttyACM is root:dialout 0660: grant ourselves access (sudo -n), as every re-enumeration needs.
    2026-10-21: the ROM came up 7 s in but unwritable, and the one-shot hint had already been spent on the
    "waiting" line, so the script timed out silently with the ROM attached."""
    t0, told_wait, told_perm = time.time(), False, False
    while time.time() - t0 < timeout:
        p = ports().get(ids)
        if p and not os.access(p, os.R_OK | os.W_OK):
            subprocess.run(["sudo", "-n", "setfacl", "-m", f"u:{os.environ['USER']}:rw", p],
                           stderr=subprocess.DEVNULL)
        if p and os.access(p, os.R_OK | os.W_OK):
            return p
        if p and not told_perm:
            print(f"{p} not writable: sudo setfacl -m u:$USER:rw {p}", file=sys.stderr)
            told_perm = True
        if not p and not told_wait and time.time() - t0 > 3:
            print(f"waiting for {what}: press 'connect to Linux' in VMware", file=sys.stderr)
            told_wait = True
        time.sleep(0.2)
    sys.exit(f"bl616_isp: no {what} after {timeout} s")


def send(port, line):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    try:
        attr = termios.tcgetattr(fd)
        attr[3] &= ~(termios.ICANON | termios.ECHO)
        termios.tcsetattr(fd, termios.TCSANOW, attr)
        os.write(fd, b"\r\n" + line.encode() + b"\r\n")
        termios.tcdrain(fd)
    finally:
        os.close(fd)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--wait-app", action="store_true", help="wait for the firmware console instead")
    ap.add_argument("--timeout", type=float, default=300)  # VMware "connect" clicks often take over a minute
    a = ap.parse_args()

    if a.wait_app:
        print(wait_for(APP, "firmware console (ffff:ffff)", a.timeout))
        return
    now = ports()
    if ROM in now:
        print(now[ROM])
        return
    if APP not in now:
        sys.exit("bl616_isp: no BL616 on USB (PR1 cable, 'connect to Linux'?); else BOOT + EN by hand")
    print(f"{now[APP]}: reboot uart", file=sys.stderr)
    send(now[APP], "reboot uart")
    print(wait_for(ROM, "BL616 ROM (349b:6160)", a.timeout))


if __name__ == "__main__":
    main()
