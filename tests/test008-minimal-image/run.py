#!/usr/bin/env python3
"""test008 automated: the current image's cold boot (5 x, milestones), temperature, VBAT and memory on both chips;
fills the minimal-image Metrics the harness measures. The image steps themselves (3-7) are build work."""
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run   # noqa: E402

r = Run(__file__)
for i in range(5):
    r.boot()
    r.sample(f"boot {i + 1}")
r.set("share", f"n/a: the whole boot is this test's subject (test022 splits it) ({r.date})")
r.finish("5 cold boots of the current image to msh; metrics from the harness.", True)
