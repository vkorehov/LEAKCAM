#!/usr/bin/env python3
"""Check the BL616 test firmware's `pt_pins` dumps (tests/test033) against the board's pin table.

    pin_check.py <state> <dump.txt> [...]      state: off | on_reset | running | boot1_high | uart_on

Each rule: the pin's function, direction and pulls the firmware must have set, and its pad level where the state
fixes it. Sources: firmware/bl616/board_pins.h, WIFI.SchDoc notes in tests/test033-bl616-pins/README.md.
"""
import re
import sys

UNUSED = [4, 5, 6, 7, 8, 9, 18, 19, 23, 24, 25, 26, 31, 32, 33, 34]   # not connected on the Ai-M62 / board
K230_SIDE = [10, 11, 12, 13, 14, 15, 21, 22]                       # 10 k pull-ups to the switched 3V3 (back-feed risk)


def rules(state):
    on = state != "off"
    running = state in ("running", "boot1_high")
    r = {
        0: ("K230_RSTN -> Q4 (1 = K230 held in reset)", dict(func="GPIO", oe=1, pu=0, pd=0, pad=0 if running else 1)),
        1: ("K230_ALIVE <- K230 (R32 100 k up to 3V3)", dict(func="GPIO", ie=1, oe=0, pu=0, pd=0)),
        2: ("AI_BOOT strap (R35 100 k down, PR1.4)", dict(oe=0, pu=0, pad=0)),
        3: ("PGOOD (R23 to 3V3_SLEEP; low = USB in)", dict(oe=0, pu=0, pd=0)),
        16: ("Y3 XTAL32K", dict(oe=0, ie=0, pu=0, pd=0)),
        17: ("Y3 XTAL32K", dict(oe=0, ie=0, pu=0, pd=0)),
        20: ("LEAK_SENS (1 M / 1 M, ADC0 / ACOMP1)", dict(func="ANALOG", oe=0, pu=0, pd=0)),
        27: ("BOOT1 via RBT1 10 k", dict(func="GPIO", oe=1, pu=0, pd=0, pad=1 if state == "boot1_high" else 0)),
        28: ("AHT20 SCL (R73 4.7 k to 3V3_SLEEP)", dict(func="I2C0", pu=0, pd=0)),
        29: ("AHT20 SDA (R74 4.7 k to 3V3_SLEEP)", dict(func="I2C0", pu=0, pd=0)),
        30: ("K230_PWR (R19 100 k down)", dict(func="GPIO", oe=1, pu=0, pd=0, pad=1 if on else 0)),
    }
    for p in K230_SIDE:
        # never driven and never pulled while the K230 is off; with the rails on, inputs (3V3 sense) or analog
        r[p] = ("K230 side, 10 k up to switched 3V3", dict(oe=0, pu=0, pd=0))
    if on:   # the link: UART0 whenever the K230 has power (pt_on starts the console bridge since test039,
             # 2026-10-23); the SDK adds its internal pull-up, harmless with the rails up
        r[21] = ("UART0 TX -> K230 UART1 RX", dict(func="UART", uart_func=2))
        r[22] = ("UART0 RX <- K230 UART1 TX", dict(func="UART", uart_func=3))
    for p in UNUSED:
        r[p] = ("not connected", dict(oe=0, pu=0, pd=0))
    return r


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    state, bad = sys.argv[1], 0
    for path in sys.argv[2:]:
        pins = {}
        for line in open(path):
            m = re.match(r"pin (\d+) raw (\w+) func (\S+)\s+(.*)", line)
            if m:
                rest = m.group(4)
                sig = re.search(r"uart_sig \d+ = func (\d+)", rest)
                kv = dict(zip(*[iter(rest.split(" uart_sig")[0].split())] * 2))
                d = dict(func=m.group(3), **{k: int(v) for k, v in kv.items() if v.isdigit()})
                if sig:
                    d["uart_func"] = int(sig.group(1))
                pins[int(m.group(1))] = d
        for pin, (what, want) in sorted(rules(state).items()):
            got = pins.get(pin)
            if got is None:
                print(f"IO{pin:02d} {what}: MISSING")
                bad += 1
                continue
            diff = [f"{k} {got.get(k)} (want {v})" for k, v in want.items() if got.get(k) != v]
            print(f"IO{pin:02d} {'ok  ' if not diff else 'FAIL'} {what}: {got['func']}" + ("; " + ", ".join(diff) if diff else ""))
            bad += bool(diff)
        print(f"{path}: {state}: {bad} pin(s) off the table")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
