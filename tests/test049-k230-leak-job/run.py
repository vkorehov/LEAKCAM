#!/usr/bin/env python3
"""test049: the K230 leak job with simulated cameras, on USB power (the bench; no hand, no phone). Each step sets the
scene on the K230 console (/sdcard/leakcam/sim), asks a check on the BL616 console (`leak check`, the RTC's path:
CHECK to the running K230) and reads the job's DONE there; step 4 also reads the server's metrics.
    tests/test049-k230-leak-job/run.py
"""
import json
import os
import re
import sys
import time
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import K230_PORT, cmd, log, openp   # noqa: E402
from phone import SERVER, board_id, firmware   # noqa: E402

DIR = "/sdcard/leakcam"
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def leak_check(secs=10):
    """`leak check` on the BL616: (result, ms CHECK -> DONE) or (None, what came)"""
    out = re.sub(r"\x1b\[[0-9;]*m", "", bl("leak check", secs, r"leak: DONE [^\n]*\n"))
    m = re.search(r"leak: DONE (\S+) (\d+) ms after the ask", out)
    return (m.group(1), int(m.group(2))) if m else (None, out.strip().splitlines()[-1:] or "nothing")


def scene(*tokens):
    """the simulated cameras' scene: appended to the scene file (the last token per camera counts)"""
    for t in tokens:
        r.k(f"echo {t} {DIR}/sim", 2)


def server_metrics(dev):
    with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}", timeout=5) as f:
        return json.loads(f.read()).get("metrics", {})


def bl_free():
    m = re.search(r"free (\d+) largest \d+ min (\d+)", bl("mem", 1.5))
    return (int(m.group(1)), int(m.group(2))) if m else None


r = Run(__file__)
dev = board_id()
fw = firmware()
if fw != "full":
    r.finish(f"not run: the board runs {fw or 'nothing'}, not the full firmware", False, switch_off=False)
    sys.exit(1)
r.fd = openp(K230_PORT)
t = time.time()
while "one running" in bl("leak", 2) and time.time() - t < 20:   # the K230's own first check at its start
    time.sleep(1)

# 1. no history
r.k(f"rm {DIR}/cam0.hist", 2)
r.k(f"rm {DIR}/cam1.hist", 2)
r.k(f"rm {DIR}/sim", 2)
res, ms = leak_check()
ls = r.k(f"ls {DIR}", 2)
check("no history: DONE new,new", res == "new,new", f"{res} in {ms} ms")
check("no history: both histories written (76800 B)", len(re.findall(r"cam[01]\.hist\s+76800", ls)) == 2)
times = [ms] if isinstance(ms, int) else []

# 2. same
res, ms = leak_check()
check("same scene: same,same", res == "same,same", f"{res} in {ms} ms")
times += [ms] if isinstance(ms, int) else []

# 3. light only
scene("cam0=light")
res, ms = leak_check()
check("light only on cam0: same,same (no change for the light alone)", res == "same,same", f"{res} in {ms} ms")

# 4. an object on cam0: changed, at the server at once
scene("cam0=object")
t0 = time.time()
res, ms = leak_check()
check("an object on cam0: changed,same", res == "changed,same", f"{res} in {ms} ms")
got = None
while time.time() - t0 < 30:
    m = server_metrics(dev)
    if m.get("job") == "changed,same" and isinstance(m.get("jago"), int) and m["jago"] <= 30:
        got = time.time() - t0
        break
    time.sleep(2)
check("an object on cam0: the server has job=changed,same within 30 s (a check-in at once)", got is not None,
      f"{got:.0f} s" if got is not None else "not in 30 s")

# 5. the other camera
scene("cam0=same", "cam1=object")
res, ms = leak_check()
check("an object on cam1: same,changed", res == "same,changed", f"{res} in {ms} ms")

# 6. the history kept on a change
scene("cam1=same")
res, ms = leak_check()
check("cam1 same again: same,same (the history was kept on the change)", res == "same,same", f"{res} in {ms} ms")

# 7. repeated checks: memory
mem0 = bl_free()
for i in range(8):
    res, ms = leak_check()
    times += [ms] if isinstance(ms, int) else []
    r.console += cmd(r.fd, None, 1)              # the job's lines, read as they come (a command drains them first)
mem1 = bl_free()
heaps = [int(x) for x in re.findall(r"job: done: t \d+ ms, heap \+(-?\d+) B", r.console)]
check("8 checks: all same,same", res == "same,same")
check("8 checks: the K230 program's heap does not grow", len(heaps) >= 8 and heaps[-1] <= heaps[-8],
      f"heap + {heaps[-8:] if heaps else 'none'} B")
check("8 checks: the BL616's free heap does not shrink", mem0 and mem1 and mem1[0] >= mem0[0] - 512,
      f"{mem0} -> {mem1}")

# 8. speed
jobs = [int(b) - int(a) for a, b in re.findall(r"job: check: t (\d+) ms.*?job: done: t (\d+) ms", r.console, re.S)]
r.k(f"rm {DIR}/sim", 2)
if times:
    r.set("Check: CHECK", f"{min(times)}-{max(times)} ms ({len(times)} checks), median {sorted(times)[len(times)//2]} ms")
if jobs:
    r.set("Job: capture", f"{min(jobs)}-{max(jobs)} ms per check, both cameras (simulated capture + imgdiff)")
r.set("Memory BL616", f"BL616 free {mem0} -> {mem1} (free, min) over 8 checks; K230 heap growth {heaps[-8:]} B")
r.set("Test run time", f"{time.time() - r.t0:.0f} s automated")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
