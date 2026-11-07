#!/usr/bin/env python3
"""test101 automated: 10 cold sessions under the BL616 test firmware's `pt_session` (rails on, reset released, the
agent's heartbeat watched, rails off 2 s after its last edge). The BL616 times its own events from power-on (first
heartbeat edge, last edge, rails off); the K230 boot milestones and the agent's READY frame (`link rx:` with the link
bridge on) are host-timed from the command, a few ms early at most."""
import os
import re
import select
import statistics
import sys
import threading
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import MILESTONES, Run, bl   # noqa: E402
from board import BL_PORT, k230, log, openp   # noqa: E402

RUNS = 10
r = Run(__file__)
r.needs("pt_boot", "pt_uart")
rows, fails = [], []


def k230_marks(fd, t0, stop, marks):
    """the boot milestones on the K230 console, s after t0"""
    buf = b""
    while not stop.is_set():
        if select.select([fd], [], [], 0.05)[0]:
            buf += os.read(fd, 65536)
            for name, pat in MILESTONES:
                if name not in marks and pat in buf:
                    marks[name] = time.time() - t0


for i in range(RUNS):
    r.off()
    time.sleep(5)                                   # rails off >= 5 s between sessions
    bl("pt_boot1 low")
    bl("pt_uart on", 1)                             # the link bridge: the agent's frames arrive as "link rx:"
    kfd = k230()
    os.read(kfd, 65536) if select.select([kfd], [], [], 0.1)[0] else None
    fd = openp(BL_PORT)
    stop, marks = threading.Event(), {}
    t0 = time.time()
    th = threading.Thread(target=k230_marks, args=(kfd, t0, stop, marks), daemon=True)
    th.start()
    os.write(fd, b"pt_session 60000\r")
    buf, ready = b"", None
    while time.time() - t0 < 90 and b"rails off" not in buf:
        if select.select([fd], [], [], 0.05)[0]:
            buf += os.read(fd, 65536)
            if ready is None and re.search(rb"link rx: \$\d+,READY", buf):
                ready = time.time() - t0
    time.sleep(0.3)
    buf += os.read(fd, 65536) if select.select([fd], [], [], 0.2)[0] else b""
    stop.set()
    th.join()
    os.close(fd)
    out = buf.decode(errors="replace")
    first = re.search(r"first heartbeat edge (\d+) ms", out)
    last = re.search(r"the last (\d+) ms after power-on", out)
    off = re.search(r"rails off (\d+) ms after power-on", out)
    if not (first and last and off):
        fails.append(f"session {i + 1}: incomplete ({'first' if not first else ''} {'last' if not last else ''} "
                     f"{'off' if not off else ''})")
        continue
    row = {"first": int(first.group(1)) / 1000, "last": int(last.group(1)) / 1000, "off": int(off.group(1)) / 1000,
           "ready": ready, **marks}
    rows.append(row)
    log("session", i + 1, {k: round(v, 3) for k, v in row.items() if v is not None})
    r.sample(f"session {i + 1}")

if rows:
    med = lambda k: statistics.median(x[k] for x in rows if x.get(k) is not None)
    mx = lambda k: max(x[k] for x in rows if x.get(k) is not None)
    have = lambda k: any(x.get(k) is not None for x in rows)
    steps = ", ".join(f"{k} {med(k):.2f}" for k, _ in MILESTONES if have(k))
    if have("ready"):
        r.set("boot", f"power-on -> READY frame {med('ready'):.2f} / {mx('ready'):.2f} s (median / max of {len(rows)}; "
                      f"{steps}; first heartbeat {med('first'):.2f} s) ({r.date})")
    r.set("share", f"the session after READY: {med('last') - med('ready'):.1f} s of agent work, then 2 s to rails off "
                   f"({r.date})" if have("ready") else None)
    r.set("speed", f"wake -> rails off {med('off'):.2f} / {mx('off'):.2f} s (median / max of {len(rows)}); the agent's last "
                   f"heartbeat {med('last'):.2f} s ({r.date})")
r.finish(f"{len(rows)} of {RUNS} sessions complete" +
         (f": rails off {min(x['off'] for x in rows):.2f}-{max(x['off'] for x in rows):.2f} s after power-on" if rows else "") +
         ("; " + "; ".join(fails) if fails else "."), not fails and len(rows) == RUNS)
