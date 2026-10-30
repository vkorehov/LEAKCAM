#!/usr/bin/env python3
"""test105 runs 1-2 automated, on USB power (no battery measurements, user 2026-10-30): the loads of Steps added one
by one on the K230 (memtest, KPU loop, encoder 2 x 15 fps, NAND writes, Wi-Fi TCP upload to the PC, both LED chains
100 %), a USB-C 1 meter reading per step (LEAKCAM_METER=ask; a state read before is reused), then all loads for
10 min: resets (SPL banner, memtest restarting), die / board temperature.
The Wi-Fi step needs the PC's sink running (tests/test100-wifi-throughput/pc_tcp_bench.py sink, port 5001); without
it the step is skipped and noted."""
import os
import re
import socket
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl   # noqa: E402
from board import log, wifi_credentials   # noqa: E402

PC, PC_PORT = "192.168.1.131", 5001
FULL_S = 600
r = Run(__file__)


def pc_sink():
    try:
        socket.create_connection((PC, PC_PORT), 2).close()
        return True
    except OSError:
        return False


wifi = pc_sink()
if wifi:                                            # the bridge before the K230 boots (as usb_handover.py)
    ssid, psk = wifi_credentials()
    bl("pt_off", 2)
    bl(f'pt_wifi store "{ssid}" "{psk}"', 3)
    bl("pt_wifi bridge", 25, r"after wifi_link_start[^\n]*\n|radio busy")
else:
    r.notes.append(f"Wi-Fi step skipped: no TCP sink on {PC}:{PC_PORT} (pc_tcp_bench.py sink)")
r.boot()
r.sample("idle")
steps = []


def step(name, start, known=None, busy=None):
    """known: a reading taken before of this same state (never read twice, user 2026-10-30); busy: a load the
    runner repeats itself while the meter is read (msh has no loops)"""
    start()
    time.sleep(5)
    a = known if known is not None else r.meter(f"test105: {name}", force=True, busy=busy)
    s = r.sample(name)
    steps.append((name, a, s.get("die")))
    log("step", name, a)


step("K230 at msh idle (agent running)", lambda: None, known=0.220)          # test006
step("+ memtest (CPU + DDR)", lambda: r.k("/sdcard/app/memtest 0 24 &", 2), known=0.244)   # test026, avg
step("+ KPU change net in a loop", lambda: r.k("/sdcard/app/leakcam_kputest 2000000 &", 2))
step("+ encoder 2 x 15 fps", lambda: r.k(f"/sdcard/app/venctest 2 {FULL_S + 300} 15 &", 2))
NAND = lambda: (r.k("cp /bin/leakcam_wake /sdcard/t105", 30), r.k("rm /sdcard/t105", 10))
step("+ NAND writes", lambda: None, busy=NAND)
if wifi:
    ip = None
    for _ in range(60):
        m = re.search(r"ip address: (?!0\.0\.0\.0)(\d+\.\d+\.\d+\.\d+)", r.k("ifconfig", 1, r"dns server"))
        if m:
            ip = m.group(1)
            break
        time.sleep(0.5)
    if ip:
        step("+ Wi-Fi TCP upload", lambda: r.k(f"iperf -c {PC} -t {FULL_S + 120} -i 10", 2))
    else:
        r.notes.append("Wi-Fi step skipped: the K230 got no address")
step("+ both LED chains 100 %", lambda: [r.k(f"pwm_set pwm {ch} 40000 40000", 1) and r.k(f"pwm_enable pwm {ch}", 1)
                                       for ch in (1, 0)])

t0, resets = time.time(), 0                         # run 2: everything on for 10 min
while time.time() - t0 < FULL_S:
    out = r.k("cp /bin/leakcam_wake /sdcard/t105", 30) + r.k("rm /sdcard/t105", 10)   # NAND writes go on
    resets += out.count("U-Boot SPL")
    r.sample(f"full load {time.time() - t0:.0f} s")
for ch in (1, 0):
    r.k(f"pwm_disable pwm {ch}", 1)
die = [x[2] for x in steps if x[2] is not None]
table = "; ".join(f"{n}: {a:.3f} A" if a else f"{n}: not read" for n, a, _ in steps)
r.set("current", table + f" (USB-C 1 VBUS meter, 5 V) ({r.date})" if any(a for _, a, _ in steps) else None)
r.set("voltage", f"n/a: on USB power, the charger rail ({r.date}); no battery measurements (user, 2026-10-30)")
r.set("share", f"n/a: loads after the boot ({r.date})")
r.finish(f"{len(steps)} load steps on USB; then all loads {FULL_S // 60} min: {resets} K230 resets." +
         (f" Die {min(die):.1f}-{max(die):.1f} C over the steps." if die else ""), resets == 0)
