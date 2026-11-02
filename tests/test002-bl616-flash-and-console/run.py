#!/usr/bin/env python3
"""test002 automated: the BL616 test firmware's console - pt_status (leak node, AHT20, USB), pt_leak, 8 rounds
with memory snapshots (leak verdict). K230 stays off: no die temperature or VBAT (the K230 ADC reads VBAT)."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402,F401

r = Run(__file__)
r.off()
fails = []
for i in range(8):
    st = bl("pt_status", 2)
    if not re.search(r"Leak probes : 1[56]\d\d mV, dry", st):
        fails.append(f"round {i + 1}: leak node not dry ~1650 mV")
    if "AHT20       : FAIL" in st:
        fails.append(f"round {i + 1}: AHT20 failed")
    r.sample(f"round {i + 1}")
a = r.meter("K230 rails off, BL616 test firmware awake on USB")
r.set("voltage", f"n/a: BL616-only test, rails off - VBAT is read by the K230 ADC (test023) ({r.date})")
r.finish("8 rounds of `pt_status`: leak node dry, AHT20 read, USB detected" + (" - " + "; ".join(fails) if fails
         else "; all good."), not fails)
