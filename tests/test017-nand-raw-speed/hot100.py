#!/usr/bin/env python3
"""test017 (2026-10-24): the adopted NAND timing hot and over time - all of /bin read again and again at the boot
defaults (1-line 80 MHz delay 5, 4-line data 100 MHz delay 6 + edge) while memtest heats the die (test025: ~60 C),
each read against the 50 MHz reference checksum taken first. The sample delay moves with temperature: today's
other checks ran at ~43 C. Usage: hot100.py [minutes] (default 60). Pass = every read clean.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup"))
from board import bl, cmd, k230, log   # noqa: E402
from mem_leak import parse_k230   # noqa: E402

MINUTES = float(sys.argv[1]) if len(sys.argv) > 1 else 60


def read(fd):
    out = cmd(fd, "nand_bench read 160", 40, r"read errors, checksum [0-9a-f]{8}[\s\S]*?MB/s[^\n]*\n")   # memtest prints too
    m = re.search(r"(\d+) read errors, checksum ([0-9a-f]{8})", out)
    r = re.search(r"checksum [0-9a-f]{8}[\s\S]*?([\d.]+) MB/s", out)
    return (int(m.group(1)) if m else None, m.group(2) if m else None, float(r.group(1)) if r else None)


def temp(fd):
    m = re.search(r"chip temperature: (-?[\d.]+)", cmd(fd, "chip_temp", 3, r"chip temperature[^\n]*\n"))
    return float(m.group(1)) if m else None


def main():
    bl("pt_off"); bl("pt_boot1 low"); bl("pt_on")
    fd = k230(); bl("pt_run"); cmd(fd, None, 8)
    cmd(fd, "nand_clk2 0", 2); cmd(fd, "nand_rxsd 5 0 5 0", 2); cmd(fd, "nand_clk 50000000", 3)
    _, ref, _ = read(fd)
    cmd(fd, "nand_clk 80000000", 3); cmd(fd, "nand_rxsd 5 0 6 1", 2); cmd(fd, "nand_clk2 100000000", 2)
    log(f"reference 50 MHz {ref}; back at the boot defaults; die {temp(fd)} C; free {parse_k230(cmd(fd, 'free', 3))}")
    log("memtest:", cmd(fd, "/sdcard/app/memtest 0 &", 3).strip().splitlines()[-1:])
    n = bad = 0
    sp, ts = [], []
    t0 = time.time()
    while time.time() - t0 < MINUTES * 60:
        e, c, mbs = read(fd)
        t = temp(fd)
        n += 1
        if e != 0 or c != ref:
            bad += 1
            log(f"read {n}: BAD errors {e} checksum {c} at {t} C")
        if mbs:
            sp.append(mbs)
        if t:
            ts.append(t)
        if n % 20 == 0:
            log(f"{n} reads, {bad} bad, {min(sp)}-{max(sp)} MB/s, die {min(ts)}-{max(ts)} C")
    log(f"free {parse_k230(cmd(fd, 'free', 3))}")
    bl("pt_off")
    log(f"{'PASS' if bad == 0 and n else 'FAIL'}: {n} reads of /bin, {bad} bad, {min(sp)}-{max(sp)} MB/s, "
        f"die {min(ts)}-{max(ts)} C over {MINUTES:g} min - rails off")


if __name__ == "__main__":
    main()
