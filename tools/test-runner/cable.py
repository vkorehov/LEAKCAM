"""The bench as the phone on USB-C 1: the setup over the cable (firmware/bl616_setup/usb_setup.c) through the K230's
relay, /dev/leakcam-k230usb (1209:abd1). The network from ~/.leakcam_wifi, the PIN from the server: neither printed.
    setup() -> (state, seconds from the first line to SAVED) or (None, reason)"""
import os
import re
import select
import time

import harness   # noqa: F401  first: it puts tools/power-bringup (board) on the path
from board import log, openp, wifi_credentials
from phone import board_pin

PORT = "/dev/leakcam-k230usb"


def relay(secs=90):
    """seconds until the relay's CDC device is there, None if not in secs"""
    t0 = time.time()
    while time.time() - t0 < secs:
        if os.path.exists(PORT):
            return time.time() - t0
        time.sleep(0.2)
    return None


class Cable:
    def __init__(self):
        self.fd = openp(PORT)
        self.buf = b""

    def close(self):
        os.close(self.fd)

    def line(self, secs):
        end = time.time() + secs
        while time.time() < end:
            if b"\n" in self.buf:
                ln, self.buf = self.buf.split(b"\n", 1)
                return ln.decode(errors="replace").strip()
            if select.select([self.fd], [], [], 0.05)[0]:
                try:
                    self.buf += os.read(self.fd, 4096)
                except OSError:
                    return None
        return None

    def ask(self, cmd, word, secs=10):
        """the answer line starting with word (OK/ERR or the command's keyword); STATUS lines pushed meanwhile kept"""
        os.write(self.fd, cmd.encode() + b"\n")
        end = time.time() + secs
        while time.time() < end:
            ln = self.line(end - time.time())
            if ln is None:
                break
            if ln.startswith("STATUS"):
                self.status.append(ln)
            if ln.split(" ")[0] in (word, "ERR"):
                return ln
        return None


def setup(secs=60):
    ssid, psk = wifi_credentials()
    c = Cable()
    c.status = []
    try:
        idl = c.ask("ID", "ID")
        if not idl or not idl.startswith("ID "):
            return None, f"ID answered {idl!r}"
        log("cable:", idl)
        for cmd, word in ((f"PIN {board_pin()}", "OK"), ("SSID " + ssid.encode().hex(), "OK"),
                          ("PSK " + psk.encode().hex(), "OK"), ("COMMIT 1 1", "OK")):
            a = c.ask(cmd, word)
            if a != "OK":
                return None, f"{cmd.split()[0]} answered {a!r}"
        t0 = time.time()
        while time.time() - t0 < secs:
            for st in c.status:
                log("cable:", st)
                m = re.match(r"STATUS (\d+)", st)
                if m and int(m.group(1)) >= 4:
                    return int(m.group(1)), time.time() - t0
            c.status = []
            ln = c.line(1)
            if ln and ln.startswith("STATUS"):
                c.status.append(ln)
        return None, "no final STATUS"
    finally:
        c.close()
