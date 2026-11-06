#!/usr/bin/env python3
"""test045: link frames reach a K230 program through /dev/link and back (DESIGN.md 5.4, k230_link.h). The BL616 test
firmware stands in for the product's link code: every READY the K230 product program (k230_app/leakcam.c) sends shows
on the BL616 shell as `link rx:`, and this runner answers it at once with `pt_uart frame` ACK, then WAKE setup. Pass:
the program logs its WAKE step after every start, and link_stat shows the driver's file ops in use (poll saw data).
2026-11-05: the driver's fops were cleared by rt_device_register(), the program's poll() never saw a frame (HISTORY).
    tests/test045-k230-link-frames/run.py
"""
import os
import re
import select
import statistics
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run   # noqa: E402
from board import BL_PORT, K230_PORT, log, openp   # noqa: E402

STARTS = 5
WAKE_WAIT_S = 15            # the program gives up after 3 READY calls (4.5 s); its WAKE step comes well before
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


r = Run(__file__)
b, k = openp(BL_PORT), openp(K230_PORT)
bufs = {b: b"", k: b""}


def say(fd, s):
    os.write(fd, s.encode() + b"\r\n")


def lines(secs, stop=None):
    """(port, line) for secs or until stop(port, line) is true; every READY seen on the BL616 is answered at once"""
    end = time.time() + secs
    while time.time() < end:
        for fd in select.select([b, k], [], [], 0.02)[0]:
            bufs[fd] += os.read(fd, 65536)
            while b"\n" in bufs[fd]:
                raw, bufs[fd] = bufs[fd].split(b"\n", 1)
                line = re.sub(r"\x1b\[[0-9;]*m", "", raw.decode(errors="replace")).rstrip()
                r.clog_write("BL616" if fd == b else "K230", line)
                m = re.search(r"link rx: \$(\d+),READY", line) if fd == b else None
                if m:
                    say(b, f"pt_uart frame {m.group(1)},ACK,{m.group(1)}")
                    say(b, "pt_uart frame 0,WAKE,setup,0,-1,0,-1,-1")
                if stop and stop(fd, line):
                    return line
    return None


def start():
    """K230 off -> on -> run (NAND boot, the product image); the time of pt_run"""
    for c, s in (("pt_off", 1.5), ("pt_uart on", 0.3), ("pt_boot1 low", 0.3), ("pt_on", 2)):
        say(b, c)
        lines(s)
    t = time.time()
    say(b, "pt_run")
    return t


ready, wake, inprog = [], [], []
for i in range(STARTS):
    t = start()
    seen = {}

    def stop(fd, line):
        if fd == b and "READY" in line and "ready" not in seen:
            seen["ready"] = time.time() - t
        m = re.search(r"leakcam: start: t (\d+) ms", line)
        if m:
            seen["start_ms"] = int(m.group(1))
        m = re.search(r"leakcam: WAKE (\w+): t (\d+) ms", line)
        if m:
            seen["reason"], seen["wake_ms"], seen["wake"] = m.group(1), int(m.group(2)), time.time() - t
        return "wake" in seen or "FATAL" in line

    last = lines(WAKE_WAIT_S, stop)
    if "wake" in seen and seen.get("reason") == "setup":
        ready.append(seen["ready"])
        wake.append(seen["wake"])
        if "start_ms" in seen:
            inprog.append(seen["wake_ms"] - seen["start_ms"])
        log(f"start {i + 1}: READY {seen['ready']:.2f} s, WAKE setup in the program {seen['wake']:.2f} s after pt_run")
    else:
        log(f"start {i + 1}: no WAKE ({last or 'nothing within the wait'})")
        r.obstacle(f"start {i + 1}: the program got no WAKE ({last or 'no line'})")

check(f"the program gets ACK + WAKE setup after every K230 start ({STARTS})", len(wake) == STARTS, f"{len(wake)}/{STARTS}")
say(k, "link_stat")
stat = ""
lines(1.5, lambda fd, line: False)
stat = open(os.path.join(r.dir, "console.log")).read().rsplit("link_stat: rx", 1)[-1]
m = re.search(r"/dev/link fops (\w+), waiting (\d+) B, opens (\d+), polls (\d+) \(data (\d+)\), reads (\d+)", stat)
check("/dev/link has the driver's file ops (link_stat)", m and m.group(1) == "attached", m.group(0) if m else "no line")
check("the program's poll() saw the frames", m and int(m.group(5)) > 0, f"polls {m.group(4)}, with data {m.group(5)}" if m else "")
fd_ = re.search(r"frames (\d+) frame_drop (\d+)", stat)
check("no frame dropped in the K230 kernel", fd_ and fd_.group(2) == "0", fd_.group(0) if fd_ else "no line")
os.close(b)
r.fd = k
r.sample("after the last start")
if wake:
    r.set("speed", f"READY -> WAKE in the program {statistics.median(inprog)} ms (median of {len(inprog)}, "
          f"max {max(inprog)} ms); WAKE {statistics.median(wake):.2f} s after pt_run" if inprog else None)
    r.set("boot", f"pt_run -> first READY {statistics.median(ready):.2f} s (median of {len(ready)}, max {max(ready):.2f} s)")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed)
sys.exit(1 if failed else 0)
