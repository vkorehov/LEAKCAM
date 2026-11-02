"""test124 steps 1-2 (no lab supply): the brown-out detector's configuration and the reset record after each reset we
can make without hands, read by `lp_bod` on the setup firmware's test bed (slot 1; the full firmware runs the same SDK
startup, System_BOD_Init). Power-on and EN-pin resets are by hand (README).

    python3 run.py            (the pair flashed, USB-C 1 in)
"""
import importlib.util
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "power-bringup"))
spec = importlib.util.spec_from_file_location("t126", os.path.join(REPO, "tests/test126-firmware-slot-switch/run.py"))
t126 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(t126)
from board import BL_PORT   # noqa: E402

ask, log, which, switch = t126.ask, t126.log, t126.which, t126.switch


def bod(tag):
    o = ask("lp_bod", 3, r"lp_bod: reset record[^\n]*")
    lines = re.findall(r"lp_bod: [^\r\n]*", o)
    for l in lines:
        log(tag, l)
    return lines


def reset_by(cmd):
    t0 = time.time()
    ask(cmd, 1)
    gone = False
    while time.time() - t0 < 40:
        if not os.path.exists(BL_PORT):
            gone = True
        elif gone and t126.slot():
            return time.time() - t0
        time.sleep(0.2)
    return None


def main():
    if which() == "full":
        switch()                                  # the slot switch: a POR reset by software (slot.c)
        bod("after a slot switch from the full firmware (bl_sys_reset_por):")
    else:
        bod("as found:")
    for cmd, tag in [("reboot", "after `reboot`:"), ("crash hang", "after a watchdog reset (`crash hang`):"),
                     ("crash test", "after an exception (`crash test`):")]:
        secs = reset_by(cmd)
        log(f"`{cmd}`: console back in {secs:.1f} s" if secs else f"`{cmd}`: no console")
        bod(tag)
    ask("crash clear", 2)


if __name__ == "__main__":
    main()
