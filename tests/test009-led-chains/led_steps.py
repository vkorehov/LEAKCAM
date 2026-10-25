#!/usr/bin/env python3
"""test009 re-run (2026-10-25): both LED chains stepped through their duty cycles on the K230's PWM, each step held
HOLD_S seconds for the user's meter reading (`led_steps.py <hold_s> top`: only the 75 / 100 % knee and both); K230 die temperature and the AHT20 after every step. Channel 1 = white
(66.7 mA at 100 %), channel 0 = IR (133 mA at 100 %); 25 kHz (40000 ns period)."""
import os, re, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup"))
from board import bl, cmd, k230, log   # noqa: E402

HOLD_S = int(sys.argv[1]) if len(sys.argv) > 1 else 15
PERIOD = 40000
WHITE, IR = 1, 0


def temps(fd):
    t = re.findall(r"-?\d+\.\d+", cmd(fd, "chip_temp", 1.5))
    a = re.findall(r"AHT20[^\n]*", bl("pt_i2c", 2))
    return f"die {t[-1] if t else '?'} C, board {a[-1].split(':', 1)[-1].strip() if a else '?'}"


def pwm(fd, ch, pct):
    if pct:
        cmd(fd, f"pwm_set pwm {ch} {PERIOD} {PERIOD * pct // 100}", 1)
        cmd(fd, f"pwm_enable pwm {ch}", 1)
    else:
        cmd(fd, f"pwm_disable pwm {ch}", 1)


bl("pt_off", 2); bl("pt_boot1 low"); bl("pt_on", 2)
fd = k230(); bl("pt_run", 0.1)
cmd(fd, None, 10, r"msh />")
time.sleep(2)
pwm(fd, WHITE, 0); pwm(fd, IR, 0)           # both defined off: an unconfigured channel lit at full (test009)
steps = [("K230 idle, LEDs off", None, 0)]
steps += [(f"white {p} %", WHITE, p) for p in (10, 25, 50, 75, 100)]
steps += [("white off", WHITE, 0)]
steps += [(f"IR {p} %", IR, p) for p in (10, 25, 50, 75, 100)]
steps += [("both 100 %", WHITE, 100), ("both off", None, 0)]
if len(sys.argv) > 2 and sys.argv[2] == "top":   # the knee only: 75 / 100 % and both, for the 10 A range
    steps = [("K230 idle, LEDs off", None, 0), ("white 75 %", WHITE, 75), ("white 100 %", WHITE, 100),
             ("white off", WHITE, 0), ("IR 75 %", IR, 75), ("IR 100 %", IR, 100), ("both 100 %", WHITE, 100),
             ("both off", None, 0)]
log(f"start: {temps(fd)}; each step {HOLD_S} s")
try:
    for name, ch, pct in steps:
        if name == "both off":
            pwm(fd, WHITE, 0); pwm(fd, IR, 0)
        elif ch is not None:
            pwm(fd, ch, pct)
        log(f">>> {name}: read the meter now")
        time.sleep(HOLD_S - 3)
        log(f"    {name}: {temps(fd)}")
finally:
    # whatever happened (a USB unplug drops the console: OSError), the LEDs and the K230 go off
    try:
        pwm(fd, WHITE, 0); pwm(fd, IR, 0)
    except OSError:
        pass
    bl("pt_off", 2)
    log("done, LEDs and K230 off")
