#!/usr/bin/env python3
"""Put the BL616 into its ROM download mode over the USB console, no BOOT link or EN touch.

    bl616_isp.py              -> prints the ROM's port (/dev/ttyACMn) once it is there
    bl616_isp.py --boot PORT  -> after flashing: ROM boots the new firmware (clears the user-boot field, resets)
    bl616_isp.py --wait-app   -> after flashing: waits for the firmware's console, prints its port

Both firmwares (test and product) have `isp` (wdg_guard.c: watchdog off, then as `reboot uart`); the SDK board bl616dk's
shell has `reboot uart`:
it sets the HBN user-boot field to 1 and does a power-on reset, and the ROM then starts as if BOOT were
high. Their console is the SDK's USB CDC (ffff:ffff); the ROM enumerates as Bouffalo CDC (349b:6160).
The field lives in HBN_RSV2 (0x2000F108), which a reset keeps: BLFlashCommand's reset alone lands in the ROM
again, only EN cleared it. --boot does what the vendor's bflb_eflash_loader.clear_boot_status() does (ISP
memory write 0x50, HBN_RSV2 = 0), then the ISP reset 0x21, in the ROM session the flasher left open (or a new one:
the ROM closes a session after ~2 s idle; then 0x55 bytes until it answers OK).
A firmware that never reaches its console cannot be switched this way: BOOT + EN by hand
(README.md section 2). The VMware host passes every re-enumerated device to Linux by itself (popups off since
2026-10-24): the ROM and the app appear within ~2 s, so a device missing after DEVICE_WAIT_S is a board fault.
"""
import argparse
import glob
import os
import subprocess
import sys
import termios
import time

ROM = ("349b", "6160")
# devices attach to Linux by themselves (no VMware prompt since 2026-10-24); ~2 s seen, 20 s is a fault
DEVICE_WAIT_S = 20
APP = ("ffff", "ffff")


def ports():
    """{(vid, pid): /dev/ttyACMn} from sysfs, interface 0 only: the firmware's second port (if02, ttyACM1) is the
    K230 console, and `reboot uart` sent there went to the K230 (2026-10-23, make flash waited for ever)."""
    found = {}
    for tty in sorted(glob.glob("/sys/class/tty/ttyACM*")):
        dev = os.path.realpath(os.path.join(tty, "device", ".."))
        try:
            ids = tuple(open(os.path.join(dev, f)).read().strip() for f in ("idVendor", "idProduct"))
            if int(open(os.path.join(tty, "device", "bInterfaceNumber")).read(), 16) != 0:
                continue
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
            print(f"waiting for {what}", file=sys.stderr)
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


def isp_cmd(fd, cmd, data=b""):
    """One ROM ISP command: id, checksum, length (LE), data; the answer is OK or FL + error code."""
    n = len(data).to_bytes(2, "little")
    termios.tcflush(fd, termios.TCIFLUSH)
    os.write(fd, bytes([cmd, (sum(n) + sum(data)) & 0xFF]) + n + data)
    ack, t0 = b"", time.time()
    while len(ack) < 2 and time.time() - t0 < 1:
        ack += os.read(fd, 4)
    return ack[:2] == b"OK", ack


def boot(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attr = termios.tcgetattr(fd)
        attr[3] &= ~(termios.ICANON | termios.ECHO)
        attr[6][termios.VMIN], attr[6][termios.VTIME] = 0, 0
        termios.tcsetattr(fd, termios.TCSANOW, attr)
        os.set_blocking(fd, True)
        clear = (0x2000F108).to_bytes(4, "little") + bytes(4)
        ok, ack = isp_cmd(fd, 0x50, clear)
        if not ok:
            # the ROM ends an ISP session after ~2 s without a command (the vendor tool sets that timeout): if the
            # flasher's container took longer to exit, hand-shake again - 0x55 bytes until it answers OK (2026-10-22)
            for _ in range(3):
                termios.tcflush(fd, termios.TCIFLUSH)
                os.write(fd, b"\x55" * 600)
                time.sleep(0.2)
                if b"OK" in os.read(fd, 64):
                    break
            ok, ack = isp_cmd(fd, 0x50, clear)
        if not ok:
            sys.exit(f"bl616_isp: HBN_RSV2 write refused ({ack!r}), also after a new handshake; tap EN")
        isp_cmd(fd, 0x21)  # the chip resets at once; the OK may not get out
    finally:
        os.close(fd)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--boot", metavar="PORT", help="ROM port: boot the flashed firmware")
    ap.add_argument("--wait-app", action="store_true", help="wait for the firmware console instead")
    ap.add_argument("--timeout", type=float, default=DEVICE_WAIT_S)
    a = ap.parse_args()

    if a.boot:
        boot(a.boot)
        return
    if a.wait_app:
        print(wait_for(APP, "firmware console (ffff:ffff)", a.timeout))
        return
    now = ports()
    if ROM in now:
        print(now[ROM])
        return
    if APP not in now:
        sys.exit("bl616_isp: no BL616 on USB (PR1 cable?); else BOOT + 3V3 cut by hand")
    # `isp` stops the watchdog first (firmware/bl616_common/wdg_guard.c); `reboot uart` left it running into the ROM, which it
    # reset every 8 s before the ROM enumerated (2026-10-23). Firmware without `isp` (older builds) gets `reboot uart`.
    print(f"{now[APP]}: isp", file=sys.stderr)
    send(now[APP], "isp")
    time.sleep(1.5)
    if APP in ports():
        print(f"{now[APP]}: reboot uart (no isp command in this firmware)", file=sys.stderr)
        send(now[APP], "reboot uart")
    print(wait_for(ROM, "BL616 ROM (349b:6160)", a.timeout))


if __name__ == "__main__":
    main()
