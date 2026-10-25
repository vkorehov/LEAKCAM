#!/usr/bin/env python3
"""test100 (2026-10-25): where the K230's Wi-Fi time goes - every K230 console line timestamped (host clock) by a reader
thread that runs from before the K230 leaves reset.
1) product path: bridge up (the BL616 joins from its stored credentials, remembered AP), K230 boot, `bl616_rescan` (what
   the agent's WIFI step does; the powertest firmware does not answer the agent's READY), `ifconfig` polled until wlan0
   has an address; 2) test path: msh `wifi disc` then `wifi join <ssid> <psk>` (RT-Thread's WLAN manager), polled the
   same way. Credentials from ~/.leakcam_wifi, never printed."""
import os, re, select, sys, threading, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup"))
from board import bl, k230, wifi_credentials   # noqa: E402

ssid, psk = wifi_credentials()
T0 = [time.time()]
lines = []                       # (host time, line)
QUIET = ("ifconfig", "network interface", "MTU", "MAC", "FLAGS", "gw address", "net mask", "dns server", "ipv6", "msh />")


def reader(fd):
    buf = b""
    while True:
        if select.select([fd], [], [], 0.02)[0]:
            try:
                buf += os.read(fd, 65536)
            except OSError:
                continue
            *ls, buf = buf.split(b"\n")
            for l in ls:
                t = l.decode(errors="replace").replace("\r", "").replace(psk, "***").strip()
                if t:
                    lines.append((time.time(), t))
                    if not t.startswith(QUIET) or re.search(r"msh />\S", t):
                        print(f"{time.time() - T0[0]:7.3f}  {t}", flush=True)


def send(fd, s):
    os.write(fd, s.encode() + b"\r\n")


def wait(pattern, secs, since):
    end = time.time() + secs
    while time.time() < end:
        for t, l in list(lines):
            if t >= since and re.search(pattern, l):
                return t
        time.sleep(0.02)
    return None


def poll_ip(fd, secs):
    since, end = time.time(), time.time() + secs
    while time.time() < end:
        send(fd, "ifconfig")
        t = wait(r"ip address: (?!0\.0\.0\.0)\d+\.\d+\.\d+\.\d+", 0.25, since)
        if t:
            print(f"{t - T0[0]:7.3f}  ==> wlan0 has an address", flush=True)
            return t
    print("  no address", flush=True)
    return None


bl("pt_off", 2); bl("pt_boot1 low"); bl("pt_on", 2)
fd = k230()
threading.Thread(target=reader, args=(fd,), daemon=True).start()
print("--- 1) product path: bridge up, K230 boots")
T0[0] = time.time()
print(bl("pt_wifi bridge", 25, r"after wifi_link_start[^\n]*\n").strip().splitlines()[-1:])
print(f"{time.time() - T0[0]:7.3f}  bridge up (BL616 joined)")
t_run = time.time(); bl("pt_run", 0.1)
print(f"{t_run - T0[0]:7.3f}  pt_run (K230 out of reset)")
t = wait(r"msh />", 10, t_run)
print(f"{(t or time.time()) - T0[0]:7.3f}  msh prompt ({(t or time.time()) - t_run:.3f} s after pt_run)")
time.sleep(1)
t_rs = time.time(); send(fd, "bl616_rescan")
print(f"{t_rs - T0[0]:7.3f}  bl616_rescan sent")
t = wait(r"wlan0|netdev started|control channel", 5, t_rs)
t_ip = poll_ip(fd, 20)
if t_ip:
    print(f"  rescan -> address {t_ip - t_rs:.3f} s")
send(fd, "bl616_stat"); time.sleep(0.5)
print("--- 2) test path: msh wifi join")
send(fd, "wifi disc"); time.sleep(3)
T0[0] = time.time()
send(fd, f"wifi join {ssid} {psk}")
t_ip = poll_ip(fd, 25)
if t_ip:
    print(f"  wifi join -> address {t_ip - T0[0]:.3f} s")
send(fd, "bl616_stat"); time.sleep(0.5)
time.sleep(1)
bl("pt_off", 2)
