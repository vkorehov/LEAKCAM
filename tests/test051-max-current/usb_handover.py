#!/usr/bin/env python3
"""test051 (2026-10-25): every load available without cameras on, first from USB, then the user unplugs USB and the
cell must carry it on with no reset for a minute. Loads: both LED chains 100 %, Wi-Fi TCP upload to the LAN PC (iperf,
PC: pc_tcp_bench.py sink), DDR + CPU (`memtest 0`). The K230 console is read throughout, reopened when the unplug
drops the port; a reset shows as an SPL banner or memtest's pass count starting again. memtest keeps RESERVE_MB free
and runs as a background job, and the runner asks for `chip_temp` every 5 s: a K230 that stalls shows at once (the
first run, 2026-10-25: memtest took 109 of 128 MB, a program failed to load and the console went silent at 19 s while
the LEDs stayed lit). Everything off at the end."""
import os, re, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup"))
from board import BL_PORT, K230_PORT, bl, cmd, k230, log, openp, wifi_credentials   # noqa: E402

PC = "192.168.1.131"
RUN_S = int(sys.argv[1]) if len(sys.argv) > 1 else 150
RESERVE_MB = 24
ssid, psk = wifi_credentials()

bl("pt_sleep 2 nousb", 2)                       # clean BL616 (test100: a leftover bridge stalls the next one)
time.sleep(4)
for _ in range(30):
    if os.path.exists(BL_PORT):
        break
    time.sleep(1)
time.sleep(2)
bl("pt_off", 2); bl("pt_boot1 low"); bl("pt_on", 2)
bl(f'pt_wifi store "{ssid}" "{psk}"', 3)
bl("pt_wifi bridge", 25, r"after wifi_link_start[^\n]*\n|radio busy")
fd = k230(); bl("pt_run", 0.1)
cmd(fd, None, 10, r"msh />")
cmd(fd, "bl616_rescan", 5, r"wlan init success|no card")
ip = None
end = time.time() + 30
while time.time() < end and not ip:
    time.sleep(0.3)
    m = re.search(r"ip address: (?!0\.0\.0\.0)(\d+\.\d+\.\d+\.\d+)", cmd(fd, "ifconfig", 0.5, r"dns server"))
    ip = m.group(1) if m else None
log("K230 address", ip)
for ch in (1, 0):                                 # white, IR at 100 %
    cmd(fd, f"pwm_set pwm {ch} 40000 40000", 1)
    cmd(fd, f"pwm_enable pwm {ch}", 1)
if ip:
    cmd(fd, f"iperf -c {PC} -t {RUN_S + 30} -i 5", 2)
os.write(fd, f"/sdcard/app/memtest 0 {RESERVE_MB} &\r\n".encode())
t0 = time.time()
log(f">>> ALL LOADS ON (LEDs 100 %, Wi-Fi upload {'on' if ip else 'OFF: no address'}, memtest): read the meter on "
    "USB now, then UNPLUG USB at ~30 s")
resets, passes, last_pass, buf = 0, 0, None, b""
drop_at = None
next_probe, probes, answers = time.time() + 5, 0, 0
while time.time() - t0 < RUN_S:
    if time.time() >= next_probe:                 # is the K230 alive? one answer per probe expected
        try:
            os.write(fd, b"chip_temp\r\n")
            probes += 1
        except OSError:
            pass
        next_probe = time.time() + 5
    try:
        b = os.read(fd, 65536)
    except BlockingIOError:
        time.sleep(0.05)
        continue
    except OSError:                               # the unplug re-enumerated the BL616's USB: reopen
        if drop_at is None:
            drop_at = time.time() - t0
            log(f"console dropped at {drop_at:.0f} s (USB unplug?), reopening")
        try:
            os.close(fd)
        except OSError:
            pass
        while time.time() - t0 < RUN_S:
            time.sleep(1)
            try:
                fd = openp(K230_PORT)
                log(f"console back at {time.time() - t0:.0f} s")
                break
            except OSError:
                pass
        continue
    buf += b
    *lines, buf = buf.split(b"\n")
    for l in lines:
        t = l.decode(errors="replace").strip()
        if "U-Boot SPL" in t:
            resets += 1
            log(f"!!! K230 RESET at {time.time() - t0:.0f} s: {t}")
        m = re.search(r"pass\D{0,3}(\d+)", t, re.I)
        if m and "memtest" not in t.lower():
            n = int(m.group(1))
            if last_pass is not None and n < last_pass:
                log(f"!!! memtest pass count restarted ({last_pass} -> {n}) at {time.time() - t0:.0f} s")
            last_pass, passes = n, passes + 1
        if re.search(r"^-?\d+\.\d+$|temp", t, re.I) and "chip_temp" not in t:
            answers += 1
            log(f"{time.time() - t0:5.0f} s  alive: {t}")
        if re.search(r"Mbps|error|fail", t, re.I):
            log(f"{time.time() - t0:5.0f} s  {t}")
log(f"after {RUN_S} s: K230 resets {resets}, memtest pass lines {passes} (last {last_pass}), chip_temp answers "
    f"{answers} of {probes}, console drop at {drop_at}")
try:
    os.write(fd, b"\x03")                         # stop memtest
    for ch in (1, 0):
        cmd(fd, f"pwm_disable pwm {ch}", 1)
    log(re.findall(r"-?\d+\.\d+", cmd(fd, "chip_temp", 1.5))[-1:], "die C at the end")
except OSError:
    pass
finally:
    bl("pt_off", 2)
    log("done: LEDs, loads and K230 off")
