#!/usr/bin/env python3
"""test027: reproduce the one-in-three hang of `venctest 1 15 0` (1 channel at the encoder's top) after the
2 x 15 fps run in the same boot (2026-10-30, repeat 2: no "venctest: done", the console quiet for 11 min).
Per boot: `venctest 2 20 15`, then `venctest 1 15 0`; every K230 console byte goes to hang_repro.log with its time.
On a hang (no "done" in 60 s): the BL616's view (K230_ALIVE heartbeat toggling?, link counters), whether msh answers
a bare return and `list_thread`, then the K230 is left on for a look and the script stops.
    hang_repro.py [boots]      default 8
"""
import os
import re
import select
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import MILESTONES, bl   # noqa: E402
from board import k230   # noqa: E402

BOOTS = int(sys.argv[1]) if len(sys.argv) > 1 else 8
LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "hang_repro.log")
log = open(LOG, "w", buffering=1)
fd = None
T0 = time.time()


def note(*a):
    s = f"{time.time() - T0:9.3f} ## " + " ".join(str(x) for x in a)
    print(s, flush=True)
    log.write(s + "\n")


def read_until(pat, secs):
    """console bytes, each line logged with its time; the text read, and whether pat came"""
    buf, end = b"", time.time() + secs
    while time.time() < end:
        if select.select([fd], [], [], 0.05)[0]:
            chunk = os.read(fd, 65536)
            buf += chunk
            for line in chunk.decode(errors="replace").splitlines():
                if line.strip():
                    log.write(f"{time.time() - T0:9.3f}    {line}\n")
            if re.search(pat, buf.decode(errors="replace")):
                return buf.decode(errors="replace"), True
    return buf.decode(errors="replace"), False


def k(c, pat, secs):
    os.write(fd, (c + "\r\n").encode())
    return read_until(pat, secs)


def boot():
    global fd
    bl("pt_off", 2)
    time.sleep(1)
    bl("pt_boot1 low")
    bl("pt_on", 2)
    fd = fd or k230()
    bl("pt_run", 0.1)
    out, ok = read_until(r"msh />", 20)
    marks = [n for n, p in MILESTONES if p.decode() in out]
    note("boot", "ok" if ok else "NO MSH", marks)
    time.sleep(1.5)
    read_until(r"$^", 0.5)
    return ok


DELAYS = [int(x) for x in os.environ.get("DELAYS", "").split(",") if x]
for b, d in enumerate(DELAYS):
    # mode 2: the 1-channel run at d s after msh, inside the agent's own session (test101: ~24.5 s of leakcam_wake
    # after READY) - does a collision with the agent's camera / MPP work hang it?
    if not boot():
        break
    time.sleep(d)
    out, ok1 = k("/sdcard/app/venctest 1 15 0", r"venctest: done", 60)
    note(f"delay {d} s: 1 channel top", "done " + str(re.findall(r"= ([\d.]+) fps", out)) if ok1 else "NOT DONE in 60 s")
    if not ok1:
        hb = bl("pt_alive 5", 8, r"half-period[^\n]*\n|edges \d+, now \w+\r?\n")
        note("heartbeat 5 s:", re.findall(r"edges \d+, now \w+|half-period[^\r\n]*", hb))
        _, answered = k("", r"msh />", 5)
        note("msh answers a bare return:", answered, "- HANG with the agent at", d, "s; K230 left on")
        sys.exit(1)
if DELAYS:
    note("no hang over the delays", DELAYS)
    bl("pt_off", 2)
    sys.exit(0)
for b in range(BOOTS):
    if not boot():
        note("boot failed: stop")
        break
    _, ok2 = k("/sdcard/app/venctest 2 20 15", r"venctest: done", 60)
    note(f"boot {b + 1}: 2 x 15 fps", "done" if ok2 else "NOT DONE")
    # the harness's r.sample() between the two runs, as in the run that hung (2026-10-30 repeat 2)
    bl("pt_status", 2)
    bl("mem", 1.5)
    s = [k("chip_temp", r"msh />", 3)[1], k("free", r"msh />", 3)[1], k("adc probe adc", r"msh />", 2)[1]]
    s += [k("adc read 1", r"msh />", 2)[1] for _ in range(8)]
    note(f"boot {b + 1}: sample commands answered {sum(s)} of {len(s)}")
    t1 = time.time()
    out, ok1 = k("/sdcard/app/venctest 1 15 0", r"venctest: done", 60)
    fps = re.findall(r"= ([\d.]+) fps", out)
    note(f"boot {b + 1}: 1 channel top", f"done in {time.time() - t1:.1f} s, {fps}" if ok1 else "NOT DONE in 60 s")
    if not (ok1 and ok2):
        st = bl("pt_status", 2)
        hb = bl("pt_alive 5", 8, r"half-period[^\n]*\n|edges \d+, now \w+\r?\n")
        note("BL616 K230_PWR:", re.findall(r"K230_PWR[^\r\n]*", st), "| heartbeat 5 s:",
             re.findall(r"edges \d+, now \w+|half-period[^\r\n]*", hb))
        _, answered = k("", r"msh />", 5)
        note("msh answers a bare return:", answered)
        out, _ = k("list_thread", r"msh />", 8)
        note("list_thread:", "answered" if "msh />" in out else "no answer", f"({len(out)} bytes)")
        out, _ = k("link_stat", r"irqs[^\n]*\n", 5)
        note("link_stat:", re.findall(r"link_stat: [^\r\n]*", out))
        note("HANG reproduced - K230 left on, log in", LOG)
        sys.exit(1)
note(f"no hang in {BOOTS} boots")
bl("pt_off", 2)
