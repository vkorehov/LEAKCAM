#!/usr/bin/env python3
"""test017 (2026-10-24): NAND data reads at 100 MHz with the 1-line phase at 80 MHz (per-phase SPI timing patch).

1-line transfers (commands, status polls) at 80 MHz, delay 5 (proven); 4-line page reads at 100 MHz (nand_clk2),
multi-line delay 2-9 on both edges, 3 reads of 64 /bin blocks each against the 50 MHz checksum. The centre of the
widest clean run on edge 0 then gets 30 more reads and its speed is compared with plain 80 MHz. Ends at plain 80 MHz.
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup"))
from board import bl, cmd, k230   # noqa: E402


def read(fd):
    out = cmd(fd, "nand_bench read 64", 25, r"MB/s[^\n]*\n")
    m = re.search(r"(\d+) read errors, checksum ([0-9a-f]{8})", out)
    r = re.search(r"([\d.]+) MB/s", out)
    return (int(m.group(1)) if m else None, m.group(2) if m else None, float(r.group(1)) if r else None)


def main():
    bl("pt_off"); bl("pt_boot1 low"); bl("pt_on")
    fd = k230(); bl("pt_run"); cmd(fd, None, 8)
    cmd(fd, "nand_clk 50000000", 3); cmd(fd, "nand_clk2 0", 2); cmd(fd, "nand_rxsd 5 0 5 0", 2)
    _, ref, _ = read(fd)
    print(f"reference 50 MHz: {ref}", flush=True)
    cmd(fd, "nand_clk 80000000", 3)
    base = [read(fd) for _ in range(3)]
    print(f"plain 80 MHz: {[b[1] == ref and b[0] == 0 for b in base]}, {[b[2] for b in base]} MB/s", flush=True)
    cmd(fd, "nand_clk2 100000000", 2)
    clean = {}
    for e in (0, 1):
        for d in range(2, 10):
            cmd(fd, f"nand_rxsd 5 0 {d} {e}", 2)
            rs = [read(fd) for _ in range(3)]
            ok = all(r[0] == 0 and r[1] == ref for r in rs)
            clean[(d, e)] = ok
            print(f"data 100 MHz delay {d} edge {e}: {'clean' if ok else 'FAIL'} "
                  f"{[r[1] for r in rs]} {[r[2] for r in rs]} MB/s", flush=True)
    runs, cur = [], []
    for d in range(2, 10):
        if clean[(d, 0)]:
            cur.append(d)
        else:
            if cur:
                runs.append(cur)
            cur = []
    if cur:
        runs.append(cur)
    if runs:
        best = max(runs, key=len)
        d = best[len(best) // 2]
        cmd(fd, f"nand_rxsd 5 0 {d} 0", 2)
        rs = [read(fd) for _ in range(30)]
        oks = sum(1 for r in rs if r[0] == 0 and r[1] == ref)
        sp = [r[2] for r in rs if r[2]]
        print(f"WINDOW edge 0 delays {best[0]}-{best[-1]}, centre {d}: {oks}/30 clean, {min(sp)}-{max(sp)} MB/s "
              f"(plain 80 MHz {base[0][2]})", flush=True)
    else:
        print("WINDOW: none on edge 0", flush=True)
    cmd(fd, "nand_clk2 0", 2); cmd(fd, "nand_rxsd 5 0 5 0", 2)
    bl("pt_off")
    print("back at plain 80 MHz, rails off", flush=True)


if __name__ == "__main__":
    main()
