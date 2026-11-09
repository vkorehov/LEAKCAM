#!/usr/bin/env python3
"""test139: USB-C 1 replug stability - no plug timing may wedge the BL616's BLE core (user 2026-11-09: "nothing
should produce wedge in a product"; case H of skill bl616-ble-low-power, the 2026-11-08 day of "not found").

The bench relay box (skill leakcam-bench-relays) unplugs and plugs USB-C 1 at chosen gaps, landing the plug edges in
each state of the setup firmware (no network stored): the unplug path (K230 stop, advertising restart, the 1 s before
the sleep), the sleep between advertisements, the plug wake (USB back, the K230 session start), bursts of very short
replugs, and an unplug around the switch to the full firmware and its way back to the setup firmware on the cell.
After every case the proof: unplugged, the phone finds the board asleep; plugged, no WEDGE in the evlog, the BLE core
alive (`blecore`), the setup firmware answering. Restores the suite's start state (the bench network, the full
firmware) at the end.
    tests/test139-replug-stability/run.py [case ...]    cases by name prefix (A, B, C, D, E, A300, Evbus, ...); default all
"""
import fcntl
import os
import re
import sys
import threading
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log   # noqa: E402
from phone import erase, board_id, firmware, op, ready, relay, to_firmware, wedge   # noqa: E402

_lock = open(__file__)
try:
    fcntl.flock(_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except OSError:
    sys.exit("test139: another run.py is running")

r = Run(__file__)
checks, times = [], {"found": []}
U, P = {1, 2, 4}, set()         # relays on: unplugged (VBUS, D+, D- off the bench) / plugged
clean = lambda t: re.sub(r"\x1b\[[0-9;]*m", "", t)


class Stop(Exception):
    """a step failed: the later steps depend on it"""


def check(name, ok, detail="", stop=False):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)
    if stop and not ok:
        raise Stop(name)


def found(secs):
    """seconds until the phone's scan sees the board, None if not in secs"""
    t0 = time.time()
    while time.time() - t0 < secs:
        res = op("scan", secs=10)
        if res.get("found") and res.get("name") == dev:
            return time.time() - t0
    return None


def console(secs=40):
    t0 = time.time()
    while time.time() - t0 < secs and not os.path.exists(BL_PORT):
        time.sleep(0.5)
    return os.path.exists(BL_PORT)


def evlog():
    return clean(bl("evlog", 3, r"evlog: (?:empty|\d+ events[\s\S]*?/>)"))


def proof(name, scan_secs=30, detail=""):
    """the case ended unplugged: found asleep; then plugged: no wedge, the setup firmware back"""
    t = found(scan_secs)
    if t is not None:
        times["found"].append(t)
    if not relay([P]):
        raise Stop("the relay box does not answer")
    up = console() and to_firmware("setup", secs=30) is not None
    w = wedge() if up else "no console"
    ok = t is not None and up and w is None
    seen = f"found {t:.1f} s" if t is not None else f"NOT found in {scan_secs} s"
    check(name, ok, f"{detail}; {seen}" + ("" if up else "; the setup firmware not back") + (f"; {w}" if w else ""))
    if not ok:      # the evidence: the evlog with its register dumps (stop on any issue: memory stop-on-any-issue)
        log("evlog after the failure:\n" + evlog())
        raise Stop(name)


def spacing(t):
    return "phases " + "/".join(str(b - a) for a, b in zip(t, t[1:])) + " ms" if len(t) > 1 else "one phase"


def case_a(d):
    """unplugged, plugged back after d ms (inside the unplug path: K230 stop, advertising restart at ~4 ms, the sleep
    at 1000 ms, then advertising every 2 s), 3 s plugged, unplugged for the proof"""
    t = []
    if not relay([U, P, U], [d, 3000], t):
        raise Stop("the relay box does not answer")
    proof(f"A{d}: replug {d} ms after the unplug", detail=spacing(t))


def case_b(d):
    """asleep 8 s (advertising), plugged (the PGOOD wake, USB back, the K230 session at ~0.1-1.6 s), unplugged d ms
    later"""
    t = []
    if not relay([U, P, U], [8000, d], t):
        raise Stop("the relay box does not answer")
    proof(f"B{d}: asleep, plugged, unplugged {d} ms later", detail=spacing(t))


def case_c(n, g):
    """n replugs in a row g ms apart (a bad cable, a plug wiggled in), ending unplugged"""
    t = []
    if not relay([U, P] * n + [U], [g] * (2 * n), t):
        raise Stop("the relay box does not answer")
    proof(f"C{n}x{g}: {n} replugs {g} ms apart", detail=spacing(t))


def case_d(w):
    """`slot switch` (setup -> full, no network: the full firmware switches back to the setup firmware) with the
    unplug landing around it: the relay connects (~3-5 s) and unplugs w ms after its no-op first phase, the switch
    sent 4 s after the relay op started; the real offset read from the evlog (the full firmware's own `usb
    unplugged`, ms after its boot) - the battery start path of bug-006, case F"""
    t = []
    th = threading.Thread(target=lambda: t.append(relay([P, U], [w], t)))
    th.start()
    time.sleep(4)
    bl("slot switch", 1)
    th.join()
    if not t or t[-1] is not True:
        raise Stop("the relay box does not answer")
    proof(f"D{w}: unplug around the switch to the full firmware", scan_secs=120,
          detail=f"unplug {w} ms after the relay's first phase; see the evlog for the offset")
    log("evlog (D offsets):\n" + "\n".join(l for l in evlog().splitlines() if " reg " not in l)[-1500:])


def case_e(name, phases, gaps):
    """the half-plugged states of the relay box's bring-up (2026-11-08 18:42-18:49, the last thing before the first
    wedge): CC alone switched, VBUS alone with D+/D- still on the PC, the wires 0.5 s apart in either order, a swap to
    the phone (it powers and enumerates the board) and back; ending unplugged"""
    t = []
    if not relay(phases, gaps, t):
        raise Stop("the relay box does not answer")
    proof(f"E {name}", detail=spacing(t))


E = {   # name: (phases of relays on, gaps ms); K1 D+, K2 D-, K3 CC, K4 VBUS; the last phase unplugged {1, 2, 4}
    "cc-only": ([{3}, P, U], [10000, 3000]),
    "vbus-only": ([{4}, U], [10000]),
    "data-first": ([{1, 2}, U], [500]),
    "power-first": ([{4}, {4, 1}, U], [500, 500]),
    "one-by-one": ([{1}, {1, 2}, {1, 2, 3}, {1, 2, 3, 4}, U], [500, 500, 500, 500]),
    "phone": ([U, {1, 2, 3, 4}, U], [1000, 10000]),
    "phone-bench": ([U, {1, 2, 3, 4}, U, P, U], [1000, 10000, 1000, 3000]),
}

CASES = ([(f"A{d}", case_a, (d,)) for d in (100, 300, 600, 900, 1000, 1100, 1300, 2000, 3000, 6000)] +
         [(f"B{d}", case_b, (d,)) for d in (50, 150, 400, 800, 1500, 3000)] +
         [(f"C{n}x{g}", case_c, (n, g)) for n, g in ((10, 100), (5, 200), (5, 500), (3, 1000))] +
         [(f"D{w}", case_d, (w,)) for w in (0, 1000, 2000, 3000, 5000)] +
         [(f"E{k}", case_e, (k, *v)) for k, v in E.items()])
want_cases = sys.argv[1:]
CASES = [c for c in CASES if not want_cases or any(c[0].startswith(p) for p in want_cases)]

if not os.path.exists(BL_PORT) or firmware() not in ("full", "setup"):
    r.obstacle("no BL616 product pair on the console: plug USB-C 1, flash the product pair")
    r.finish("not run: no product pair on the console.", False, switch_off=False)
    sys.exit(1)
why = ready(r.notes)        # the phone, and no wedge before the first case
if why:
    r.obstacle(why)
    r.finish(f"not run: {why.split(' (')[0]}.", False, switch_off=False)
    sys.exit(1)
dev = board_id()
t_start = time.time()
try:
    # the setup firmware with no network: it sleeps and advertises on the cell
    if not relay([P]):
        raise Stop("the relay box does not answer")
    erase()
    t = to_firmware("setup", secs=90)
    check("no network: the setup firmware", t is not None, f"{t:.0f} s" if t else str(firmware()), stop=True)
    bl("evlog clear", 2, r"cleared")
    for name, fn, args in CASES:
        log(f"case {name}: {fn.__doc__.split(chr(10))[0]}")
        fn(*args)
except Stop as e:
    stopped = str(e)
    log("stopped at:", e)
else:
    stopped = ""
finally:
    relay([P])

run_s = time.time() - t_start
f = sorted(times["found"])
r.set("speed", f"{len(CASES)} cases in {run_s / 60:.0f} min; found asleep after each: median "
               f"{f[len(f) // 2]:.1f} s, max {f[-1]:.1f} s ({r.date})" if f else "no case reached its proof")
r.sample("after the cases")
# the suite's start state back: the bench network, the full firmware (run_tests.product_start)
from run_tests import product_start   # noqa: E402
product_start()

failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" if d else n for n, _, d in failed) + "."
if stopped:
    summary += f" Stopped at: {stopped}."
ok = not failed and not stopped
r.finish(summary, ok, switch_off=False)
sys.exit(0 if ok else 1)
