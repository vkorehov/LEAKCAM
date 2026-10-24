#!/usr/bin/env python3
"""test017 (2026-10-24): verify the adopted NAND timing (install.sh: 1-line 80 MHz delay 5, 4-line data 100 MHz delay
6 + edge) as booted, without touching it first.

Boot times, K230 free memory and chip temperature at the start; 10 reads of all of /bin (160 blocks) and 10 program +
read-back runs over the spare 32 blocks at the boot defaults (quad programs run at the data clock too); then the
reference: the same /bin read at 50 MHz, 1-line timing on both phases. Pass = every default read gives the reference
checksum with 0 errors and every write run 0 errors. Memory and temperature again at the end (free must not fall).
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup"))
from board import bl, cmd, k230, log   # noqa: E402
from mem_leak import parse_k230   # noqa: E402

BLOCKS = 160          # /bin, 20 MiB


def read(fd):
    out = cmd(fd, f"nand_bench read {BLOCKS}", 40, r"MB/s[^\n]*\n")
    m = re.search(r"(\d+) read errors, checksum ([0-9a-f]{8})", out)
    r = re.search(r"([\d.]+) MB/s", out)
    return (int(m.group(1)) if m else None, m.group(2) if m else None, float(r.group(1)) if r else None)


def write(fd):
    out = cmd(fd, "nand_bench write 32", 60, r"read back[^\n]*\n")
    m = re.search(r"(\d+) bad \(skipped\), (\d+) errors", out)
    sp = re.findall(r"(program|read back)[^\n]*?([\d.]+) MB/s", out)
    return (int(m.group(2)) if m else None, {k: float(v) for k, v in sp})


def state(fd, tag):
    log(tag, "free:", parse_k230(cmd(fd, "free", 3)))
    log(tag, "temp:", re.findall(r"[^\n]*temp[^\n]*", cmd(fd, "chip_temp", 3)))


def main():
    bl("pt_off"); bl("pt_boot1 low"); bl("pt_on")
    fd = k230(); bl("pt_run"); cmd(fd, None, 8)
    log("boot_times:", cmd(fd, "boot_times", 3).strip().replace("\n", " | "))
    log("timing:", cmd(fd, "nand_rxsd", 2).strip(), "|", cmd(fd, "nand_clk2", 2).strip())
    state(fd, "start")
    rs = [read(fd) for _ in range(10)]
    ws = [write(fd) for _ in range(10)]
    cmd(fd, "nand_clk2 0", 2); cmd(fd, "nand_rxsd 5 0 5 0", 2); cmd(fd, "nand_clk 50000000", 3)
    _, ref, ref_mbs = read(fd)
    oks = sum(1 for r in rs if r[0] == 0 and r[1] == ref)
    wok = sum(1 for w in ws if w[0] == 0)
    sp = [r[2] for r in rs if r[2]]
    log(f"reference 50 MHz: {ref} ({ref_mbs} MB/s)")
    log(f"default reads: {oks}/10 clean, {min(sp) if sp else '?'}-{max(sp) if sp else '?'} MB/s, "
        f"checksums {sorted(set(r[1] for r in rs))}")
    log(f"writes: {wok}/10 without errors, {[w[1] for w in ws[:2]]} ...")
    state(fd, "end")
    bl("pt_off")
    log("PASS" if oks == 10 and wok == 10 else "FAIL", "- rails off")


if __name__ == "__main__":
    main()
