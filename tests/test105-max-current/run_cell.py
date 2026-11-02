#!/usr/bin/env python3
"""test105 run 3 on the cell (user 2026-10-31: cell-only runs unblocked; measured on the board: VBAT by the K230 ADC,
resets, die / board temperature; no meter on the cell side): USB-C 1 out, PR1 (console) stays in. A cold start into
every camera-free load at once (KPU loop, encoder 2 x 15 fps, NAND writes, Wi-Fi TCP upload, memtest, both LED chains
100 %), held until VBAT under load reaches STOP_MV or the K230 resets; then all off and the rest voltage after 60 s.
rev 1 has no hardware discharge cutoff (bug-004): STOP_MV keeps the run well above the 3.10 V cutoff.
    tests/test105-max-current/run_cell.py
"""
import os
import re
import socket
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import log, wifi_credentials   # noqa: E402

PC, PC_PORT = "192.168.1.131", 5001
STOP_MV = 3300                 # VBAT under the full load: stop here (cutoff 3.10 V, bug-004)
MAX_S = 3 * 3600
LONG_S = MAX_S + 600           # the loads' own run time: longer than the run
r = Run(__file__)


def usb_power():
    m = re.search(r"USB cable\s*:\s*(detected|not detected)", bl("pt_status", 2))
    return None if not m else m.group(1) == "detected"


def wait_usb(present, secs=900):
    t0 = time.time()
    while time.time() - t0 < secs:
        if usb_power() == present:
            return True
        time.sleep(1)
    return False


def bridge():
    ssid, psk = wifi_credentials()
    bl(f'pt_wifi store "{ssid}" "{psk}"', 3)
    out = bl("pt_wifi bridge", 25, r"after wifi_link_start[^\n]*\n|radio busy|rails off")
    if "after wifi_link_start" not in out:
        r.obstacle("Wi-Fi bridge did not start: " + out.strip().splitlines()[-1][:80])


def started(name, out):
    bad = re.search(r"(?i).*(failed|not found|out of memory).*", re.sub(r".*not use fast load.*", "", out))
    if bad:
        r.obstacle(f"{name}: did not start: {bad.group(0).strip()[:100]}")
        r.finish(f"stopped at '{name}': the load did not start", False)
        sys.exit(1)


bl("pt_off", 2)
if usb_power():
    log("ACTION: unplug USB-C 1 now (PR1, the console, stays in)")
    if not wait_usb(False):
        r.finish("not run: USB-C 1 stayed in.", False, switch_off=False)
        sys.exit(1)
log("on the cell: USB-C 1 out")
wifi = True
try:
    socket.create_connection((PC, PC_PORT), 2).close()
except OSError:
    wifi = False
    r.notes.append(f"Wi-Fi load skipped: no TCP sink on {PC}:{PC_PORT} (pc_tcp_bench.py sink)")

t_on = time.time()
r.boot(after_on=bridge if wifi else None)          # the cold start on the cell
s = r.sample("idle on the cell")
v_idle = s.get("vbat")
started("KPU", r.k("/sdcard/app/leakcam_kputest 200000000 &", 2) + r.k("", 4))
started("encoder", r.k(f"/sdcard/app/venctest 2 {LONG_S} 15 &", 2) + r.k("", 4))
if wifi:
    r.k("bl616_rescan", 5, r"wlan init success|no card")
    ip = None
    for _ in range(60):
        m = re.search(r"ip address: (?!0\.0\.0\.0)(\d+\.\d+\.\d+\.\d+)", r.k("ifconfig", 1, r"dns server"))
        if m:
            ip = m.group(1)
            break
        time.sleep(0.5)
    if ip:
        started("Wi-Fi", r.k(f"iperf -c {PC} -t {LONG_S} -i 60", 2) + r.k("", 4))
    else:
        r.notes.append("Wi-Fi load skipped: the K230 got no address")
started("memtest", r.k("/sdcard/app/memtest 0 24 &", 2) + r.k("", 4))
for ch in (1, 0):
    r.k(f"pwm_set pwm {ch} 40000 40000", 1)
    r.k(f"pwm_enable pwm {ch}", 1)
t0, resets, vmin, why = time.time(), 0, None, "time limit"
r.leak_from = len(r.samples)
while time.time() - t0 < MAX_S:
    out = r.k("cp /bin/leakcam_wake /sdcard/t105", 30) + r.k("rm /sdcard/t105", 10)   # the NAND writes
    if "U-Boot SPL" in out:
        resets += 1
        why = "K230 reset"
        r.obstacle(f"K230 reset under the full load on the cell at {time.time() - t0:.0f} s")
        break
    s = r.sample(f"full load on the cell {time.time() - t0:.0f} s")
    v = s.get("vbat")
    if v:
        vmin = v if vmin is None else min(vmin, v)
        if v <= STOP_MV:
            why = f"VBAT {v} mV under load"
            break
run_s = time.time() - t0
for ch in (1, 0):
    r.k(f"pwm_disable pwm {ch}", 1)
r.k("killall memtest", 2)
r.k("killall venctest", 2)
r.k("killall leakcam_kputest", 2)
r.k("killall iperf", 2)
time.sleep(60)
rest = r.sample("rest, loads off, 60 s").get("vbat")
bl("pt_off", 2)
log("ACTION: plug USB-C 1 back in (the cell charges)")
r.set("voltage", f"VBAT under every camera-free load on the cell: idle {v_idle} mV, min {vmin} mV over {run_s / 60:.0f} min, "
                 f"rest {rest} mV after 60 s off (K230 ADC ch 1) ({r.date})")
r.set("share", f"n/a: loads after the boot ({r.date})")
r.set("energy", f"n/a: no current on the cell side; the board drew ~1.13 A at this load on USB (test105 run 1) ({r.date})")
r.finish(f"cell, every camera-free load: {run_s / 60:.1f} min until {why}; VBAT {v_idle} mV idle -> {vmin} mV min under "
         f"load, {rest} mV at rest; {resets} K230 resets.", resets == 0 and (vmin or 0) > 3100)
