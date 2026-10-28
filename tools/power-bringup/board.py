"""Shared helpers for unattended LEAKCAM test runners (2026-10-24: runners must not live in a scratch folder).

  bl(command, secs=1.5, until=None)   one BL616 shell command on BL_PORT (opened per call), its output
  k230()                              the K230 console K230_PORT (raw), waits up to 60 s for the port
  cmd(fd, command, secs, until=None)  send a line (or None: only read) and read until `until` (regex) or `secs`;
                                      drains what is already waiting first, so a leftover prompt cannot end the
                                      read early (tests/test100 Obstacles)
  wifi_credentials()                  (ssid, psk) from ~/.leakcam_wifi, never from the repo
  log(*parts)                         time-stamped line to stdout

The K230 console interleaves kernel log lines with shell output character by character: judge results by counters
or exact checksums, not by long text matches (tests/test100).
"""
import os
import re
import select
import termios
import time

# the BL616's two CDC ports by USB interface, not ttyACM0/1: a phone on adb adds its own ACM port, and a port held
# open across a BL616 reset shifts the numbers (tests/test040, 2026-10-28: the board came back as ttyACM1/ttyACM3)
BL_PORT = "/dev/serial/by-id/usb-Bouffalo_Lab_Bouffalo_Lab_USB_Console_00000001-if00"
K230_PORT = "/dev/serial/by-id/usb-Bouffalo_Lab_Bouffalo_Lab_USB_Console_00000001-if02"


def log(*parts):
    print(time.strftime("%H:%M:%S ") + " ".join(str(p) for p in parts), flush=True)


def openp(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = a[1] = a[3] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd


def cmd(fd, c, secs, until=None):
    if c is not None:
        t1 = time.time()
        while time.time() - t1 < 0.3 and select.select([fd], [], [], 0.05)[0]:
            try:
                os.read(fd, 65536)
            except OSError:
                break
        data, t1 = c.encode() + b"\r\n", time.time()
        while data:                                  # non-blocking port: a full buffer says EAGAIN, wait for room
            try:
                data = data[os.write(fd, data):]
            except BlockingIOError:
                if time.time() - t1 > 5:
                    raise
                select.select([], [fd], [], 0.1)
    out = b""
    t0 = time.time()
    while time.time() - t0 < secs:
        if select.select([fd], [], [], 0.05)[0]:
            try:
                out += os.read(fd, 65536)
            except OSError:
                break
        if until and re.search(until.encode(), out):
            break
    return out.decode(errors="replace").replace("\r", "")


def bl(c, secs=1.5, until=None):
    for _ in range(30):
        try:
            fd = openp(BL_PORT)
            break
        except OSError:
            time.sleep(1)
    else:
        log("BL616 port missing")
        return ""
    r = cmd(fd, c, secs, until)
    os.close(fd)
    return r


def k230():
    for _ in range(60):
        try:
            return openp(K230_PORT)
        except OSError:
            time.sleep(1)
    return None


def wifi_credentials():
    ssid = psk = None
    for line in open(os.path.expanduser("~/.leakcam_wifi")):
        k, _, v = line.strip().partition("=")
        if k == "ssid":
            ssid = v
        elif k == "psk":
            psk = v
    return ssid, psk
