#!/usr/bin/env python3
"""test109 (BL616 part): the check-in over mTLS (DESIGN.md 5.3.12 design decision and "Built", SECURITY.txt 4). On USB
power at the bench, no hand: the credentials in flash as provisioned, every check-in a TLS session (version, suite,
handshake time, session heap), nothing plain on the boards' port, the date rule (no clock: the first check ignores only
the dates and sets the clock; a clock past the certificates' end: refused; back without a clock: taken again), a client
without a device certificate refused by the server, and the heap over repeated sessions.
    tests/test109-secure-server/run.py
"""
import os
import re
import socket
import ssl
import sys
import time
import zlib
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import REPO, Run, bl   # noqa: E402
from board import log   # noqa: E402
from phone import BOARD_SERVER, as_board, board_id, clear_leak, firmware, intervals   # noqa: E402

REPEATS = 10                     # sessions for the heap's leak check
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def plain(s):
    return re.sub(r"\x1b\[[0-9;]*m", "", s)


def session(secs=20, no_clock=False):
    """a check-in now; its TLS line (tls: <version> <suite> in N ms[, no clock...], session heap N B) or the refusal.
    no_clock: waits for the line of a session without a clock - one already under way when the clock was cleared
    (the start check-in after a flash) prints its own first (2026-11-10)"""
    want = r"tls: (TLSv\S+ \S+ in \d+ ms, no clock[^\n]*|[^\n]*(refused|handshake)[^\n]*)\n" if no_clock else \
        r"tls: (TLSv\S+ \S+ in \d+ ms[^\n]*|[^\n]*(refused|handshake)[^\n]*)\n"
    # to the check-in's end ([WCHK] check of ...: state): the tls line comes at the handshake, the answer and its X-Time
    # after it - a clock read or set right after the tls line raced the answer (clock 0 read, then the late X-Time
    # overwrote the 2049 clock: 2026-11-11)
    out = plain(bl("presence now", secs, "(?s)" + want + r".*?\[WCHK\] check of [^\n]*state 0x"))
    m = re.search(r"tls: (TLSv\S+) (\S+) in (\d+) ms(, no clock: dates not checked)?, session heap (\d+) B", out)
    return m, out


def clock():
    m = re.search(r"clock: (\d+) \(set (\d+) times\)", plain(bl("clock", 1.5)))
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


def bl_free():
    m = re.search(r"free (\d+) largest \d+ min (\d+)", bl("mem", 1.5))
    return (int(m.group(1)), int(m.group(2))) if m else None


r = Run(__file__)
dev = board_id()
fw = firmware()
if fw != "full":
    r.finish(f"not run: the board runs {fw or 'nothing'}, not the full firmware", False, switch_off=False)
    sys.exit(1)
clear_leak(dev)
intervals(dev)

# 1. provisioned: the CA, the board's certificate and key in flash, the same bytes as .secrets/pki
pki = os.path.join(REPO, ".secrets", "pki")
want = {"ca": open(os.path.join(pki, "ca.der"), "rb").read(),
        "crt": open(os.path.join(pki, "devices", f"{dev}.crt.der"), "rb").read(),
        "key": open(os.path.join(pki, "devices", f"{dev}.key.der"), "rb").read()}
out = plain(bl("provision", 3, r"tls_key (none|len \d+ crc \S+)"))
have = {n: (int(a), c) for n, a, c in re.findall(r"tls_(ca|crt|key) len (\d+) crc ([0-9a-f]{8})", out)}
check("provisioned: tls_ca, tls_crt, tls_key in flash match .secrets/pki (length + CRC only)",
      have == {n: (len(b), "%08x" % zlib.crc32(b)) for n, b in want.items()}, str(have))

# 2. a check-in is a TLS 1.3 session with the board's certificate
m, out = session()
check("a check-in over TLS 1.3, ECDHE + AES-GCM", m is not None and m.group(1) == "TLSv1.3" and "GCM" in m.group(2),
      m.group(0) if m else out.strip()[-160:])
if m:
    r.set("Handshake", f"{m.group(1)} {m.group(2)}: handshake {m.group(3)} ms, session heap {m.group(5)} B")

# 3. the boards' port: no answer without a device certificate (the server's side)
ctx = ssl.create_default_context(cafile=os.path.join(pki, "ca.crt"))
try:
    with socket.create_connection(BOARD_SERVER, timeout=5) as s, ctx.wrap_socket(s, server_hostname="leakcam-server") as t:
        t.sendall(b"POST /v1/poll?id=" + dev.encode() + b" HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n")
        refused = not t.recv(64)
except (ssl.SSLError, OSError):
    refused = True
check("the boards' port: a client without a device certificate gets nothing", refused)
st, _ = as_board("000000000001", f"/v1/poll?id={dev}", b"ssid=t109")
check("another device's certificate cannot report for this board (403)", st == 403, str(st))

# 4. the date rule: no clock -> the first check ignores only the dates and sets the clock
bl("clock 0", 1)
m, out = session(no_clock=True)
now, sets = clock()
check("no clock: the check-in taken (dates not checked) and its X-Time sets the clock",
      m is not None and m.group(4) is not None and now and abs(now - time.time()) <= 5,
      (m.group(0) if m else out.strip()[-160:]) + f"; clock {now} vs {int(time.time())}")
# a clock past the certificates' end (2046): the server's certificate refused, the clock stays wrong
bl("clock 2500000000", 1)
m, out = session()
check("a clock in 2049 (past the 20 years): the server's certificate refused, nothing sent",
      m is None and "refused" in out, out.strip()[-160:])
now, _ = clock()
check("the refused session did not move the clock", now and now >= 2500000000, str(now))
# no clock again: taken, the clock right
bl("clock 0", 1)
m, out = session(no_clock=True)
now, _ = clock()
check("no clock again: taken, the clock set from the verified answer", m is not None and now and
      abs(now - time.time()) <= 5, f"clock {now}")

# 5. repeated sessions: heap kept (each session freed at its end)
# heap compared session to session: the K230 (on all the time on USB) holds ~27 KB more or less depending on its own
# state, so a sample from before the loop is not comparable (171 KB -> 144 KB with no leak, 2026-11-10)
# a sample after every session; the K230's own state moves the free heap ~27 KB between samples (2026-11-11), a leak
# lowers every one: the best of the last three against the first
times, frees = [], []
for i in range(REPEATS):
    m, _ = session(30)
    if m:
        times.append(int(m.group(3)))
    f = bl_free()
    if f:
        frees.append(f)
mem0 = frees[0] if frees else None
mem1 = max(frees[-3:]) if len(frees) >= 3 else None
check(f"{REPEATS} sessions: every one up, the heap kept (after the 1st vs after the last)", len(times) == REPEATS
      and mem0 and mem1
      and mem1[0] >= mem0[0] - 1024, f"{len(times)}/{REPEATS} up, free {mem0} -> {mem1}")
if times:
    r.set("Handshake times", f"{REPEATS} check-ins: handshake min {min(times)} / avg {sum(times) // len(times)} / "
          f"max {max(times)} ms (TLS 1.3, P-256, the BL616's ECC/AES/SHA hardware)")
r.set("Memory BL616", f"free {mem0} -> {mem1} over {REPEATS} sessions (free, min)")
r.set("Test run time", f"{time.time() - r.t0:.0f} s automated")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
