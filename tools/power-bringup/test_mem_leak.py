#!/usr/bin/env python3
"""Test cases for mem_leak.py: stable runs pass, real leaks of each kind are caught, noise and a warm-up are not."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mem_leak import leak_verdict, parse_bl616, parse_k230  # noqa: E402


def snap(free, blocks=300, deleted=0):
    return {"free": free, "blocks": blocks, "deleted": deleted}


def check(name, series, want_leak, **kw):
    leak, text = leak_verdict(series, **kw)
    ok = leak == want_leak
    print(f"{'ok  ' if ok else 'FAIL'} {name}: {text}")
    return ok


cases = [
    ("stable heap", [snap(60000)] * 10, False),
    ("noise +-300 B", [snap(60000 + (300 if i % 2 else -300)) for i in range(10)], False),
    ("warm-up allocation, then stable", [snap(60000)] + [snap(52000)] * 9, False),
    ("steady leak 512 B per cycle", [snap(60000 - 512 * i) for i in range(10)], True),
    ("deleted tasks never freed (test100)", [snap(60000 - 900 * i, deleted=i) for i in range(8)], True),
    ("blocks rise, bytes hidden by noise", [snap(60000, blocks=300 + i) for i in range(10)], True),
    ("one drop at the end only (not steady)", [snap(60000)] * 9 + [snap(57000)], False),
]
results = [check(n, s, w) for n, s, w in cases]

line = "mem: free 61234 largest 40960 min 52000 blocks 312 used 98765 tasks 18 deleted 2"
p = parse_bl616("x\r\n" + line + "\r\nmsh />")
results.append(p == [{"free": 61234, "largest": 40960, "min": 52000, "blocks": 312, "used": 98765, "tasks": 18,
                      "deleted": 2}])
print(f"{'ok  ' if results[-1] else 'FAIL'} parse BL616 mem line")
k = parse_k230("memheap   pool size  max used size available size\n"
               "heap      8388608    1390848       7025152\nmsh />")
results.append(k == [{"pool": 8388608, "max_used": 1390848, "free": 7025152}])
print(f"{'ok  ' if results[-1] else 'FAIL'} parse K230 free table")

print(f"{sum(results)}/{len(results)} passed")
sys.exit(0 if all(results) else 1)
