#!/usr/bin/env python3
"""test019 automated: the K230 image flashed over USB boot (flash_nand.sh: erase + write), timed, the bus speed from
lsusb -t; then the image boots. This run flashes the image currently built in the SDK (the test image)."""
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl, REPO, KDIMG   # noqa: E402,F401

r = Run(__file__)
flash = os.path.join(REPO, "firmware", "k230_board", "flash_nand.sh")
r.boot(boot1="high", until=b"zzz", secs=3, expect_msh=False)
for _ in range(20):
    if "29f1:0230" in subprocess.run(["lsusb"], capture_output=True, text=True).stdout:
        break
    time.sleep(0.5)
tree = subprocess.run(["lsusb", "-t"], capture_output=True, text=True).stdout
t0 = time.time()
p = subprocess.run(["sh", flash], capture_output=True, text=True, timeout=900)
total = time.time() - t0
open(os.path.join(r.dir, "flash_last.txt"), "w").write(p.stdout[-20000:] + p.stderr[-5000:])
ok = p.returncode == 0 and "flashed" in p.stdout
r.off()
bl("pt_boot1 low")
size = os.path.getsize(KDIMG)
r.boot()
r.sample("after flash")
r.set("share", f"n/a: flashing is not part of a boot ({r.date})")
r.set("perday", f"n/a: flashing is a factory / update step ({r.date})")
r.set("energy", f"n/a: on USB power, never from the cell ({r.date})")
r.set("image", f"kdimg {size} B (test image: product programs + test tools) ({r.date})")
r.finish(f"flash_nand.sh (erase 0..248M + write {size} B): {total:.1f} s, {'ok' if ok else '**FAILED**'}; "
         f"{size / total / 1e6:.2f} MB/s overall incl. erase; then booted to msh. Log: flash_last.txt", ok)
