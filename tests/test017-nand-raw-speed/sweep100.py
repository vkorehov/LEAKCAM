#!/usr/bin/env python3
"""test017 (2026-10-24): find the NAND's 100 MHz read sample point with per-phase delays (driver patch in
firmware/k230_board/install.sh, "per-phase RX sample delay").

Reference checksum of 64 /bin blocks at 50 MHz, then at 100 MHz every 1-line delay/edge x multi-line delay/edge
(nand_rxsd d e dm em): a combination is clean when the read gives the reference checksum and 0 errors (the driver's
status alone is not trusted: a mis-sampled status says "ready, no error", test017). The best point is the clean one
farthest from a failing neighbour (the middle of the window); it is read 10 more times and timed. The bus goes back
to 80 MHz at the end: nothing is adopted here.

Needs the BL616 test firmware on /dev/ttyACM0 and the K230 console on /dev/ttyACM1 (helpers in
tools/power-bringup/board.py). Output: one line per combination in sweep100.csv, the verdict on stdout.
"""
import csv
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup"))
from board import bl, cmd, k230   # noqa: E402

READ = r"nand_bench read: [^\n]*checksum ([0-9a-f]{8})"
D = range(0, 11)
E = (0, 1)


def read(fd, timeout=20):
    out = cmd(fd, "nand_bench read 64", timeout, READ + r"[^\n]*\n(?:.*\n){0,3}")
    m = re.search(r"(\d+) read errors, checksum ([0-9a-f]{8})", out)
    r = re.search(r"([\d.]+) MB/s", out)
    return (int(m.group(1)), m.group(2)) if m else (None, None), (float(r.group(1)) if r else None), out


def main():
    bl("pt_off"); time.sleep(1); bl("pt_boot1 low"); bl("pt_on")
    fd = k230(); bl("pt_run"); cmd(fd, None, 8)
    cmd(fd, "nand_clk 50000000", 3)
    (err, ref), _, _ = read(fd)
    print(f"reference at 50 MHz: checksum {ref}, errors {err}", flush=True)
    if ref is None:
        sys.exit("no reference read")
    cmd(fd, "nand_clk 100000000", 3)
    clean = {}
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "sweep100.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["delay", "edge", "multi_delay", "multi_edge", "errors", "checksum", "clean", "MB_s"])
        for d in D:
            for e in E:
                for dm in D:
                    for em in E:
                        cmd(fd, f"nand_rxsd {d} {e} {dm} {em}", 2)
                        (err, ck), mbs, _ = read(fd)
                        ok = err == 0 and ck == ref
                        clean[(d, e, dm, em)] = ok
                        w.writerow([d, e, dm, em, err, ck, int(ok), mbs])
                        f.flush()
                print(f"1-line delay {d} edge {e}: clean multi points "
                      f"{[(dm, em) for dm in D for em in E if clean[(d, e, dm, em)]]}", flush=True)

    def margin(k):           # half steps to the nearest failing point along each axis
        d, e, dm, em = k
        def run(axis_vals, make):
            best = 0
            for step in range(1, 30):
                vals = [make(s) for s in (step, -step)]
                if any(not clean.get(v, False) for v in vals):
                    return step
            return best
        h1 = run(None, lambda s: ((2 * d + e + s) // 2, (2 * d + e + s) % 2, dm, em))
        h2 = run(None, lambda s: (d, e, (2 * dm + em + s) // 2, (2 * dm + em + s) % 2))
        return min(h1, h2)

    good = [k for k, v in clean.items() if v]
    if not good:
        print("100 MHz: no clean combination", flush=True)
        cmd(fd, "nand_clk 80000000", 3); cmd(fd, "nand_rxsd 5 0 5 0", 2)
        return
    best = max(good, key=margin)
    print(f"100 MHz: {len(good)} clean of {len(clean)}; best {best} (margin {margin(best)} half steps)", flush=True)
    cmd(fd, "nand_rxsd {} {} {} {}".format(*best), 2)
    runs = [read(fd) for _ in range(10)]
    oks = sum(1 for (e2, c2), _, _ in runs if e2 == 0 and c2 == ref)
    speeds = [m for _, m, _ in runs if m]
    print(f"best point, 10 reads: {oks}/10 clean, {min(speeds) if speeds else '?'}-{max(speeds) if speeds else '?'} "
          f"MB/s", flush=True)
    cmd(fd, "nand_clk 80000000", 3); cmd(fd, "nand_rxsd 5 0 5 0", 2)
    print("back at 80 MHz, delay 5", flush=True)


if __name__ == "__main__":
    main()
