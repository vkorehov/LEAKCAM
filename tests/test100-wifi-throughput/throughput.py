#!/usr/bin/env python3
"""test100 step 4 (2026-10-25): TCP throughput K230 <-> LAN PC over the bridge. The PC runs pc_tcp_bench.py: `sink` for
the upload (K230 `iperf -c <pc>`), `send` for the download (K230 `iperf -s`; the runner prints "SERVER UP <ip>" and
waits DOWN_S for the PC's sender). Credentials from ~/.leakcam_wifi (stored in the BL616 first), never printed. Memory
(BL616 `mem`, K230 `free`), link counters (`bl616_stat`) and RSSI before and after."""
import os, re, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup"))
from board import BL_PORT, bl, cmd, k230, log, wifi_credentials   # noqa: E402

PC = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.131"
UP_S, DOWN_S = 10, 25
ssid, psk = wifi_credentials()


def mem(fd, tag):
    b = re.findall(r"free[^\n]*", bl("mem", 2))
    k = [l.strip() for l in cmd(fd, "free", 1.5).splitlines() if re.search(r"\d{4,}", l)]
    log(f"{tag}: BL616 {b[:1]} | K230 {' | '.join(k)[:200]}")


# a clean BL616 first: the test firmware does not take a bridge fully down between runs (a second run stalled in
# "Device not in RUN state" and starved the shell, 2026-10-25); a 2 s HBN is a reset
bl("pt_sleep 2 nousb", 2)
time.sleep(4)
for _ in range(30):
    if os.path.exists(BL_PORT):
        break
    time.sleep(1)
time.sleep(2)
bl("pt_off", 2); bl("pt_boot1 low"); bl("pt_on", 2)
log(bl(f'pt_wifi store "{ssid}" "{psk}"', 3).replace(psk, "***").strip().splitlines()[-2:])
log(re.findall(r"[^\n]*(?:wifi_link_start|associated|join|connect|radio)[^\n]*",
               bl("pt_wifi bridge", 25, r"after wifi_link_start[^\n]*\n|radio busy").replace(psk, "***")))
fd = k230(); bl("pt_run", 0.1)
cmd(fd, None, 8, r"msh />")
log(cmd(fd, "bl616_rescan", 5, r"wlan init success|no card").strip().splitlines()[-1:])
ip = None
end = time.time() + 30
while time.time() < end:
    time.sleep(0.3)
    m = re.search(r"ip address: (?!0\.0\.0\.0)(\d+\.\d+\.\d+\.\d+)", cmd(fd, "ifconfig", 0.5, r"dns server"))
    if m:
        ip = m.group(1); break
log("K230 address", ip)
if not ip:
    bl("pt_off", 2); sys.exit(1)
log(re.findall(r"pt_wifi status[^\n]*", bl("pt_wifi status", 2)))
mem(fd, "before")
log(f"--- upload: iperf -c {PC} -t {UP_S}")
out = cmd(fd, f"iperf -c {PC} -t {UP_S} -i 1", UP_S + 6, r"(?i)stopped|disconnect|closed|error")
for l in out.splitlines():
    if re.search(r"(?i)mbps|kbps|bit|error|connect|stop", l):
        log("  ", l.strip())
cmd(fd, "iperf --stop", 2)
log("after upload", re.findall(r"bl616_stat:[^\n]*", cmd(fd, "bl616_stat", 1)))
log(f"--- download: iperf -s, SERVER UP {ip}")
cmd(fd, "iperf -s -i 1", 1)
print(f"SERVER UP {ip}", flush=True)
out = cmd(fd, None, DOWN_S)
for l in out.splitlines():
    if re.search(r"(?i)mbps|kbps|bit|error|connect|stop", l):
        log("  ", l.strip())
cmd(fd, "iperf --stop", 2)
log(re.findall(r"bl616_stat:[^\n]*", cmd(fd, "bl616_stat", 1)))
if len(sys.argv) > 2:                    # UDP up at the given rate (PC: pc_tcp_bench.py usink)
    log(f"--- UDP up: iperf -u -c {PC} -b {sys.argv[2]} -t {UP_S}")
    out = cmd(fd, f"iperf -u -c {PC} -b {sys.argv[2]} -t {UP_S} -i 1", UP_S + 6, r"(?i)stopped|error")
    for l in out.splitlines():
        if re.search(r"(?i)mbps|kbps|error", l):
            log("  ", l.strip())
    cmd(fd, "iperf --stop", 2)
    log(re.findall(r"bl616_stat:[^\n]*", cmd(fd, "bl616_stat", 1)))
log(re.findall(r"pt_wifi status[^\n]*", bl("pt_wifi status", 2)))
mem(fd, "after")
bl("pt_off", 2)
