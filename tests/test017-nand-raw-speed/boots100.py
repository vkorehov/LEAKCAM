#!/usr/bin/env python3
"""test017 (2026-10-24): 10 cold boots at the adopted NAND timing (4-line data 100 MHz): each boot must reach msh with
/bin and /sdcard mounted (boot_times) and read all of /bin with the 50 MHz reference checksum (verify100.py)."""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup"))
from board import bl, cmd, k230, log   # noqa: E402

REF = sys.argv[1] if len(sys.argv) > 1 else "79270e8e"


def main():
    ok = 0
    for i in range(10):
        bl("pt_off"); bl("pt_boot1 low"); bl("pt_on")
        fd = k230(); bl("pt_run"); cmd(fd, None, 8)
        bt = re.findall(r"([\d.]+) s  (?:env init done|app init done)", cmd(fd, "boot_times", 3))
        out = cmd(fd, "nand_bench read 160", 40, r"MB/s[^\n]*\n")
        m = re.search(r"(\d+) read errors, checksum ([0-9a-f]{8})", out)
        r = re.search(r"([\d.]+) MB/s", out)
        good = len(bt) == 2 and m and m.group(1) == "0" and m.group(2) == REF
        ok += bool(good)
        log(f"boot {i + 1}: mounts/app init {bt} s, read {m.groups() if m else None} {r.group(1) if r else '?'} MB/s",
            "ok" if good else "FAIL")
        os.close(fd)
    bl("pt_off")
    log(f"{ok}/10 cold boots clean - rails off")


if __name__ == "__main__":
    main()
