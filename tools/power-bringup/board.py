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
import subprocess
import termios
import time

# the BL616's two CDC ports by USB interface, not ttyACM0/1: a phone on adb adds its own ACM port, and a port held
# open across a BL616 reset shifts the numbers (tests/test040, 2026-10-28: the board came back as ttyACM1/ttyACM3).
# udev links (99-leakcam.rules): the serial number is the board's device ID since use case 2 (tests/test111)
BL_PORT = "/dev/leakcam-bl616"
K230_PORT = "/dev/leakcam-k230"


def log(*parts):
    print(time.strftime("%H:%M:%S ") + " ".join(str(p) for p in parts), flush=True)


def openp(path):
    """a console port, raw; OSError also when its device is going (a firmware switch: the node is still there but
    tcgetattr fails with EIO, 2026-11-07 test117), so callers retry it like a missing port"""
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        a = termios.tcgetattr(fd)
        a[0] = a[1] = a[3] = 0
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        termios.tcsetattr(fd, termios.TCSANOW, a)
    except termios.error as e:
        os.close(fd)
        raise OSError(*e.args) from e
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


XHCI = "0000:0a:00.0"   # this VM's USB controller (VMware passthrough)


def xhci_reset():
    """the VM's USB controller stops (`hub_ext_port_status failed`, err -32), mostly when the BL616 re-enumerates at
    a firmware switch (2026-11-05/06, test113 3 times): unbind + bind brings every device back"""
    log("BL616 port missing: the VM's USB controller reset (xhci unbind/bind)")
    for op in ("unbind", "bind"):
        subprocess.run(["sudo", "-n", "tee", f"/sys/bus/pci/drivers/xhci_hcd/{op}"], input=XHCI, text=True,
                       capture_output=True)
        time.sleep(2)


def bl(c, secs=1.5, until=None):
    for i in range(150):                # 30 s, polled every 0.2 s (no fixed waits, user 2026-11-09)
        try:
            fd = openp(BL_PORT)
            break
        except OSError:
            if i == 50 and "hub_ext_port_status failed" in subprocess.run(
                    ["sudo", "-n", "dmesg"], capture_output=True, text=True).stdout[-2000:]:
                xhci_reset()        # only the controller's fault: a sleeping BL616 has no port either
            time.sleep(0.2)
    else:
        log("BL616 port missing")
        return ""
    if i >= 50:
        cmd(fd, "", 1)          # the first command after a reconnect got lost once (the erase, 2026-11-06)
    r = cmd(fd, c, secs, until)
    os.close(fd)
    return r


def bl_back(secs=60):
    """after a BL616 reboot or switch: its port gone (up to 3 s), then back with a shell that answers; seconds, None if
    not in secs. In place of fixed sleeps after `reboot` (user 2026-11-09: "no need to wait")"""
    t0 = time.time()
    while time.time() - t0 < 3 and os.path.exists(BL_PORT):
        time.sleep(0.05)
    while time.time() - t0 < secs:
        if os.path.exists(BL_PORT):
            try:
                if re.search(r"leakcam-build|/>", bl("slot", 1, r"leakcam-build \S+")):
                    return time.time() - t0
            except OSError:
                pass
        time.sleep(0.1)
    return None


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
