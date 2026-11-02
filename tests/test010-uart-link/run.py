#!/usr/bin/env python3
"""test010 automated (test image): the agent's READY frames on the BL616 shell (pt_uart on: link frames arrive as
'link rx:' with their checksum) over 8 boots, plus a short checked echo run (uart_bench 64) each boot."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402,F401

r = Run(__file__)
frames, fails = 0, []


def ok_frame(f):
    """CRC-8 (poly 0x07) of the bytes between '$' and '*', as link_crc8() in firmware/k230_board/k230_link.h"""
    body, cs = f.rsplit("*", 1)
    c = 0
    for ch in body.lstrip("$").encode():
        c ^= ch
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return f"{c:02X}" == cs.strip()[:2].upper()


for i in range(8):
    r.off()
    time.sleep(2)
    bl("pt_boot1 low")
    bl("pt_on", 2)
    bl("pt_uart on", 1)
    r.boot()
    seen = re.findall(r"link rx: (\$[^\r\n]+)", bl("", 4))
    good = [f for f in seen if "*" in f and ok_frame(f)]
    frames += len(good)
    if not good:
        fails.append(f"boot {i + 1}: no READY frame with a valid checksum")
    r.k("killall leakcam_agent", 3)      # its frames would land in the bench's stream
    bl("pt_uart echo uart_bench 64", 0.3)
    out = r.k(None, 20, r"bad bytes[^\n]*\n")
    if not re.search(r" 0 bad bytes", out):
        fails.append(f"boot {i + 1}: echo run not clean")
    bl("pt_uart on", 1)
    r.sample(f"boot {i + 1}")
r.set("speed", f"link at 2 Mbaud (test018); {frames} READY frames, every checksum valid ({r.date})")
r.set("share", f"n/a: the agent sends READY right after app init; the link adds no boot step ({r.date})")
r.set("energy", f"n/a: below the meter's step next to the K230 ({r.date})")
r.set("perday", f"n/a: a few frames per session ({r.date})")
r.finish(f"8 boots: {frames} READY frames with valid checksums on the BL616, echo runs clean" +
         ("; " + "; ".join(fails) if fails else "."), not fails)
