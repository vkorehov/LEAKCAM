#!/usr/bin/env python3
"""test017: cold boots at the adopted NAND timing (50 MHz since 2026-10-30, kernel and U-Boot/SPL): each boot
must reach msh with /bin and /sdcard mounted (boot_times) and read all of /bin with the 50 MHz reference checksum
(verify100.py). "kernel init start" is the time from reset release through ROM, SPL (loads OpenSBI + kernel from
the NAND), DDR training and OpenSBI. Usage: boots.py [checksum [boots]]"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup"))
from board import bl, cmd, k230, log   # noqa: E402

REF = sys.argv[1] if len(sys.argv) > 1 else "79270e8e"
N = int(sys.argv[2]) if len(sys.argv) > 2 else 10
RAW = os.path.join(os.path.dirname(os.path.abspath(__file__)), "boots_console.txt")   # every boot's console


def main():
    ok = 0
    raw = open(RAW, "w")
    for i in range(N):
        bl("pt_off"); bl("pt_boot1 low"); bl("pt_on")
        fd = k230(); bl("pt_run")
        boot = cmd(fd, None, 8)
        times = cmd(fd, "boot_times", 3)
        bt = re.findall(r"([\d.]+) s  (?:kernel init start|env init done|app init done)", times)
        out = cmd(fd, "nand_bench read 160", 40, r"MB/s[^\n]*\n")
        raw.write(f"===== boot {i + 1}\n{boot}\n{times}\n{out}\n"); raw.flush()
        m = re.search(r"(\d+) read errors, checksum ([0-9a-f]{8})", out)
        r = re.search(r"([\d.]+) MB/s", out)
        good = len(bt) == 3 and m and m.group(1) == "0" and m.group(2) == REF
        ok += bool(good)
        log(f"boot {i + 1}: kernel start/mounts/app init {bt} s, read {m.groups() if m else None} {r.group(1) if r else '?'} MB/s",
            "ok" if good else "FAIL")
        os.close(fd)
    bl("pt_off")
    log(f"{ok}/{N} cold boots clean - rails off")


if __name__ == "__main__":
    main()
