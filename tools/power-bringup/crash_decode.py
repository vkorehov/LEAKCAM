#!/usr/bin/env python3
"""Decode a BL616 crash record (firmware/bl616/crash_rec.c) into functions and source lines.

    tools/power-bringup/crash_decode.py [ELF] < console.txt        (default ELF: the test firmware's build)
    tools/power-bringup/crash_decode.py --board [ELF]              (reads `crash` from the board's console)

The ELF must be the one on the board (the record holds raw addresses). addr2line is the T-Head toolchain's, x86-64
only: on an arm64 host it runs in the K230 build container (firmware/bl616/host.mk).
"""
import os
import platform
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
TOOL = os.path.join(HW, "toolchain_gcc_t-head_linux", "bin", "riscv64-unknown-elf-addr2line")
IMAGE = "k230-rtos-sdk-build:arm64-x86tc"
ELF = os.path.join(HERE, "..", "..", "tests", "firmware", "bl616_powertest", "build", "build_out", "bl616_powertest_bl616.elf")


def addr2line(elf, addrs):
    cmd = [TOOL, "-f", "-C", "-p", "-e", elf] + addrs
    if platform.machine() == "aarch64":
        cmd = ["docker", "run", "--rm", "-v", f"{HW}:{HW}", IMAGE] + cmd
    return subprocess.run(cmd, capture_output=True, text=True).stdout.splitlines()


args = [a for a in sys.argv[1:] if not a.startswith("--")]
elf = os.path.abspath(args[0]) if args else ELF
if "--board" in sys.argv:
    sys.path.insert(0, HERE)
    from board import bl
    text = bl("crash", 2)
else:
    text = sys.stdin.read()
text = re.sub(r"\x1b\[[0-9;]*m", "", text)
head = re.search(r"crash #\d+: [^\r\n]*", text)
bt = re.search(r"crash backtrace:([^\r\n]*)", text)
if not head:
    sys.exit("no crash record in the text")
print(head.group(0))
regs = dict(re.findall(r"(mepc|ra) (0x[0-9a-f]+)", head.group(0)))
named = [(k, regs[k]) for k in ("mepc", "ra") if k in regs and regs[k] != "0x00000000"]
stack = bt.group(1).split() if bt else []
lines = addr2line(elf, [a for _, a in named] + stack)
for (k, a), l in zip(named, lines):
    print(f"  {k:4} {a}  {l}")
print("  stack, return-address candidates (top first; a few can be stale):")
for a, l in zip(stack, lines[len(named):]):
    print(f"       {a}  {l}")
