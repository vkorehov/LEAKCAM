#!/usr/bin/env python3
"""test017 (2026-10-24): is the 100 MHz 4-line data the right data, captured one clock late?

The W25N02KV drives each bit up to 7 ns (tCLQV) after the falling clock edge: at 100 MHz with the board's round
trip that is past the next rising edge, so the controller may capture every nibble one clock late. nand_dump the
same pages at 50 MHz and at 100 MHz (each multi-line sample delay 0-15, both edges; 1-line delay 2, whose status
reads work) and look for the 50 MHz bytes shifted by whole nibbles (4-line: one clock = 4 bits).
Output: per setting the best nibble shift and how many nibbles match; a full match = the fix is the dummy cycles.
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup"))
from board import bl, cmd, k230   # noqa: E402

NPAGES = 3          # pages with varied data, found at 50 MHz (erased pages are all ff and show nothing)
N = 32


def dump(fd, blk, pg):
    out = cmd(fd, f"nand_dump {blk} {pg} {N}", 8, r"nand_dump: [^\n]*\n")
    m = re.search(r"status (-?\d+):((?: [0-9a-f]{2})+)", out)
    return (int(m.group(1)), bytes.fromhex(m.group(2).replace(" ", ""))) if m else (None, b"")


def nibbles(b):
    return [x for byte in b for x in (byte >> 4, byte & 15)]


def best_shift(ref, got):
    r, g = nibbles(ref), nibbles(got)
    best = (0, 0)
    for s in range(-4, 5):
        pairs = [(r[i], g[i + s]) for i in range(len(r)) if 0 <= i + s < len(g)]
        hits = sum(1 for a, b in pairs if a == b)
        if hits > best[1]:
            best = (s, hits)
    return best


def main():
    bl("pt_off"); bl("pt_boot1 low"); bl("pt_on")
    fd = k230(); bl("pt_run"); cmd(fd, None, 8)
    cmd(fd, "nand_clk 50000000", 3); cmd(fd, "nand_rxsd 5 0 5 0", 2)
    ref = {}
    for blk in range(0, 400):
        for pg in (0, 7, 31):
            st, b = dump(fd, blk, pg)
            if len(set(b)) >= 12:                       # varied bytes: a shift would show
                ref[(blk, pg)] = b
                break
        if len(ref) == NPAGES:
            break
    PAGES = list(ref)
    for p, b in ref.items():
        print(f"50 MHz block {p[0]} page {p[1]}: {b.hex(' ')}", flush=True)
    cmd(fd, "nand_clk 100000000", 3)
    for dm in range(0, 16):
        for em in (0, 1):
            cmd(fd, f"nand_rxsd 2 0 {dm} {em}", 2)
            res = []
            for p in PAGES:
                st, got = dump(fd, *p)
                s, hits = best_shift(ref[p], got)
                res.append(f"shift {s:+d} nibbles {hits}/{2 * N}")
            print(f"100 MHz multi delay {dm:2d} edge {em}: " + " | ".join(res), flush=True)
    cmd(fd, "nand_clk 80000000", 3); cmd(fd, "nand_rxsd 5 0 5 0", 2)
    bl("pt_off")
    print("back at 80 MHz, rails off", flush=True)


if __name__ == "__main__":
    main()
