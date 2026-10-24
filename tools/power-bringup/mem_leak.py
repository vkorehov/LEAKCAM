#!/usr/bin/env python3
"""Memory metrics and leak detection for LEAKCAM tests (2026-10-24, user rule: memory metrics in every test).

Snapshots:
  BL616 `mem`  -> "mem: free F largest L min M blocks B used U tasks T deleted D"   (firmware/bl616/mem_stat.c)
  K230  `free` -> RT-Thread memheap table ("heap  pool size  max used size  available size")

A test runs an operation N times and takes a snapshot after each cycle (and one before). leak_verdict() fits a line
through the series after a warm-up (first allocations of a feature are not a leak) and calls it a leak when free
memory falls steadily - more than `min_drop` bytes over the run, down in at least half the cycles - or the
number of live allocations or not-yet-freed deleted tasks keeps rising. Why: a Wi-Fi join hung after a few cycles
because deleted tasks were never freed; the free heap had fallen the whole time (tests/test100, 2026-10-24).

  mem_leak.py report <file>    the snapshot lines in a test log -> table + verdict
"""
import re
import sys

MEM_RE = re.compile(r"mem: free (\d+) largest (\d+) min (\d+) blocks (\d+) used (\d+) tasks (\d+) deleted (\d+)")
K230_HEAP_RE = re.compile(r"^heap\s+(\d+)\s+(\d+)\s+(\d+)", re.M)


def parse_bl616(text):
    """every `mem` line in text, in order, as dicts"""
    keys = ("free", "largest", "min", "blocks", "used", "tasks", "deleted")
    return [dict(zip(keys, map(int, m.groups()))) for m in MEM_RE.finditer(text)]


def parse_k230(text):
    """every RT-Thread `free` heap line: pool, max used, available -> dicts with free = available"""
    return [{"pool": int(a), "max_used": int(b), "free": int(c)} for a, b, c in K230_HEAP_RE.findall(text)]


def slope(ys):
    """least-squares slope per sample"""
    n = len(ys)
    if n < 2:
        return 0.0
    mx = (n - 1) / 2
    my = sum(ys) / n
    den = sum((i - mx) ** 2 for i in range(n))
    return sum((i - mx) * (y - my) for i, y in enumerate(ys)) / den


def leak_verdict(series, warmup=1, min_drop=1024):
    """series: snapshots (dicts with 'free', optionally 'blocks'/'deleted'), one per cycle, the first before cycle 1.
    Returns (leak: bool, text)."""
    s = series[warmup:]
    if len(s) < 3:
        return False, "too few cycles for a verdict (need >= 3 after the warm-up)"
    free = [x["free"] for x in s]
    drop = free[0] - free[-1]
    k = slope(free)
    reasons = []
    falls = sum(1 for a, b in zip(free, free[1:]) if b < a)
    if drop > min_drop and k < 0 and falls * 2 >= len(free) - 1:   # steady: down in at least half the cycles
        reasons.append(f"free falls {-k:.0f} B per cycle ({drop} B over {len(free) - 1} cycles)")
    for key in ("blocks", "deleted"):
        if key in s[0]:
            v = [x[key] for x in s]
            if v[-1] > v[0] and slope(v) > 0.5:
                reasons.append(f"{key} rise {v[0]} -> {v[-1]}")
    if reasons:
        return True, "LEAK: " + "; ".join(reasons)
    return False, f"no leak: free {free[0]} -> {free[-1]} B (slope {k:+.0f} B per cycle) over {len(free) - 1} cycles"


def report(path):
    text = open(path, errors="replace").read()
    for name, series in (("BL616", parse_bl616(text)), ("K230", parse_k230(text))):
        if not series:
            continue
        print(f"{name}: {len(series)} snapshots")
        for i, x in enumerate(series):
            print(f"  {i:3d} " + " ".join(f"{k} {v}" for k, v in x.items()))
        print("  " + leak_verdict(series)[1])


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "report":
        report(sys.argv[2])
    else:
        sys.exit(__doc__)
