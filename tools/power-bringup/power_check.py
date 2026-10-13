#!/usr/bin/env python3
"""Power bring-up of a new MIFA board, one phase at a time, with a multimeter.

  python3 power_check.py off       power off: rail-to-GND resistance (shorts)
  python3 power_check.py blank     USB on, BL616 still blank: only VBAT and 3V3_SLEEP up
  python3 power_check.py flashed   BL616 flashed: K230_PWR high, all five regulators up
  python3 power_check.py all       the three phases in order, stopping at the first failure

For each point it names the pads to probe (see top.png / top_power_zoom.png, numbers match),
reads the value you type, checks it against testpoints.json and appends it to
power_check_<board>.csv in the current directory. Type s to skip a point, q to stop.
A failing reading stops the phase: a wrong rail can damage the K230, find the cause first.
"""
import csv
import datetime
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ORDER = ("off", "blank", "flashed")
STEPS = {
    "off": "Unplug USB and the battery. Black lead on the GND pad, red on the rail pad.",
    "blank": "Plug USB-C 1 (USBC1, the charger input), no battery. Do not connect PR1 yet.",
    "flashed": "Flash the BL616 through PR1 (firmware/BUILD.md section 4.3: BOOT held, EN pulsed,\n"
               "  make flash COMX=/dev/ttyACM0), release BOOT, pulse EN. Measure within the first\n"
               "  minutes: the first boot keeps the rails on while it waits for the K230.",
}


def ask(prompt):
    while True:
        v = input(prompt).strip().lower().replace(",", ".")
        if v in ("s", "q"):
            return v
        try:
            if v.endswith("mv"):
                return float(v[:-2]) / 1000.0
            if v.endswith("k"):
                return float(v[:-1]) * 1000.0
            return float(v.rstrip("v"))
        except ValueError:
            print("  a number please (e.g. 3.31, 820, 1.2k, 15mv), s = skip, q = quit")


def run_phase(phase, data, board, log):
    ph = data["phases"][phase]
    pts = {p["rail"]: p for p in data["points"]}
    print(f"\n=== {phase}: {ph['title']}\n  {STEPS[phase]}\n  {ph['hint']}\n")
    failed = False
    for rail, (lo, hi) in ph["check"].items():
        p = pts[rail]
        rng = (f">= {lo}" if hi is None else f"<= {hi}" if lo is None else f"{lo} .. {hi}") + f" {ph['unit']}"
        v = ask(f"[{p['id']:2}] {rail:10} + {p['pad']:6} GND {p['gnd']:6} ({rng}): ")
        if v == "q":
            return None
        if v == "s":
            status = "SKIP"
        else:
            ok = (lo is None or v >= lo) and (hi is None or v <= hi)
            status = "OK" if ok else "FAIL"
            if not ok:
                print(f"  FAIL {rail}: {v:g} {ph['unit']}, expected {rng}. {p['note']}")
                failed = True
        log.writerow([datetime.datetime.now().isoformat(timespec="seconds"), board, phase, p["id"], rail,
                      p["pad"], p["gnd"], "" if v == "s" else v, ph["unit"], status])
        if failed:
            print(f"  phase {phase} stopped at {rail}: power off and find the cause before going on.")
            return False
    print(f"  phase {phase}: all measured points within limits")
    return True


def main():
    phases = sys.argv[1:] or ["all"]
    if phases == ["all"]:
        phases = list(ORDER)
    if any(p not in ORDER for p in phases):
        raise SystemExit(__doc__)
    data = json.load(open(os.path.join(HERE, "testpoints.json")))
    board = input("board id (sticker / serial, for the log): ").strip() or "unnamed"
    path = f"power_check_{board}.csv"
    new = not os.path.exists(path)
    with open(path, "a", newline="") as f:
        log = csv.writer(f)
        if new:
            log.writerow(["time", "board", "phase", "id", "rail", "pad", "gnd", "value", "unit", "status"])
        for ph in phases:
            r = run_phase(ph, data, board, log)
            if r is not True:
                break
    print(f"\nlog: {os.path.abspath(path)}")


if __name__ == "__main__":
    main()
