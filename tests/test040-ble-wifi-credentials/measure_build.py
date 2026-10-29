#!/usr/bin/env python3
"""test040 build comparison (-O2 / -O3 / LTO, Metrics "Optimised build"): on the flashed test firmware, after a
watchdog reset: BLE start -> advertising (5x, host-timed), BL616 heap after BLE start, then Wi-Fi joins with BLE
advertising (1 full, 5 remembered): associate + WPA and DHCP ms as the firmware times them, the internet check,
and whether the passphrase or the session key dump reached the console (sdk_patch.py).
Prints only the parsed numbers: the console echoes the join command with the passphrase."""
import os
import re
import statistics
import sys
import termios
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup"))
from board import BL_PORT, bl, cmd, openp, wifi_credentials   # noqa: E402


def reset():
    bl("pt_hang", 1)
    time.sleep(4)
    for _ in range(40):
        try:
            if "pt_ble" in bl("help", 2):
                return
        except (OSError, termios.error):    # the port re-enumerates after the reset
            pass
        time.sleep(0.5)
    sys.exit("BL616 console did not come back")


def ble_start():
    fd = openp(BL_PORT)
    cmd(fd, "", 0.5)
    t0, out = time.time(), ""
    cmd(fd, "pt_ble on", 0.0)
    while time.time() - t0 < 10 and "advertising as" not in out:
        out += cmd(fd, None, 0.02)
    os.close(fd)
    return time.time() - t0 if "advertising as" in out else None


def join(ssid, psk, mode="", net=False):
    out = bl(f"pt_wifi join {ssid} {psk} {mode}".strip(), 40, r"join to address \d+ ms|not associated|no DHCP")
    a = re.search(r"associated \+ WPA in (\d+) ms", out)
    d = re.search(r"DHCP in (\d+) ms", out)
    n = re.search(r"DNS to answer (\d+) ms", bl("pt_wifi net", 10, r"DNS to answer \d+ ms|failed")) if net else None
    out = out.replace(f"pt_wifi join {ssid} {psk}", "") + bl("pt_wifi leave", 6, r"pt_wifi leave[^\n]*\n|\$ ")
    if psk in out:                                   # the command's own echo removed above
        secrets.add("passphrase")
    if re.search(r"\[\d\d\] ([0-9A-F]{2}:){5}[0-9A-F]{2} +\d+-Bit", out):
        secrets.add("key dump")
    return (int(a.group(1)) if a else None, int(d.group(1)) if d else None, int(n.group(1)) if n else None)


ssid, psk = wifi_credentials()
secrets = set()
starts = []
for _ in range(5):
    reset()
    starts.append(ble_start())
m = re.search(r"mem: free (\d+)[^\n]*min (\d+)", bl("mem", 2, r"mem: [^\n]*\n"))
full = join(ssid, psk, net=True)
rem = [join(ssid, psk, "remembered") for _ in range(5)]
image = os.path.getsize(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup",
                                     "bl616_powertest", "build", "build_out", "bl616_powertest_bl616.bin"))
ok = [s for s in starts if s]
print("image", image, "B")
print("BLE start -> advertising median %.3f s (%s)" % (statistics.median(ok), " ".join("%.3f" % s for s in ok))
      if ok else "BLE start: no advertising")
print("heap after BLE start: free %s min %s" % (m.group(1), m.group(2)) if m else "mem: no answer")
print("join full: associate+WPA %s ms, DHCP %s ms, internet check (DNS to answer) %s ms" % full)
print("join remembered x5: associate+WPA %s ms" % [a for a, _, _ in rem])
print("console during joins and leaves:", "ON THE CONSOLE: " + ", ".join(sorted(secrets)) if secrets
      else "no passphrase, no key dump")
