#!/usr/bin/env python3
"""Decode a BL616 crash record (firmware/bl616_common/crash_rec.c) into functions and source lines.

    tools/power-bringup/crash_decode.py [ELF] < console.txt        (default ELF: the record's own build, below)
    tools/power-bringup/crash_decode.py --board [ELF]              (reads `crash` from the board's console)

The ELF must be the build that wrote the record (raw addresses). The record names it, "crash #N (<image> <version>)"
(crash_rec.c, firmware/VERSION), and `make flash` keeps every flashed ELF as ~/.cache/leakcam-elf/<image>-<version>.elf
(firmware/bl616_common/host.mk): with no ELF given that one is used. The full and the setup firmware share one record, so the
running image is not necessarily the one that crashed (tests/test126). addr2line is the T-Head toolchain's, x86-64
only: on an arm64 host it runs in the K230 build container (firmware/bl616_common/host.mk).
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
ELF_KEEP = os.path.expanduser("~/.cache/leakcam-elf")


def addr2line(elf, addrs):
    cmd = [TOOL, "-f", "-C", "-p", "-e", elf] + addrs
    if platform.machine() == "aarch64":
        d = os.path.dirname(elf)            # the kept ELFs live outside HW (~/.cache/leakcam-elf): mounted too
        cmd = ["docker", "run", "--rm", "-v", f"{HW}:{HW}", "-v", f"{d}:{d}:ro", IMAGE] + cmd
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"addr2line failed: {r.stderr.strip()}")
    return r.stdout.splitlines()


args = [a for a in sys.argv[1:] if not a.startswith("--")]
if "--board" in sys.argv:
    sys.path.insert(0, HERE)
    from board import bl
    text = bl("crash", 2)
else:
    text = sys.stdin.read()
text = re.sub(r"\x1b\[[0-9;]*m", "", text)
head = re.search(r"crash #\d+ \(([a-z_]+) ([0-9A-Za-z.+-]+)\): [^\r\n]*", text)
bt = re.search(r"crash backtrace:([^\r\n]*)", text)
if not head:
    sys.exit("no crash record in the text")
print(head.group(0))
elf = os.path.abspath(args[0]) if args else os.path.join(ELF_KEEP, f"{head.group(1)}-{head.group(2)}.elf")
if not os.path.exists(elf):
    sys.exit(f"no ELF {elf}: the build that wrote the record was not flashed by make flash here (give its ELF)")
print(f"  ELF {elf}")
regs = dict(re.findall(r"(mepc|ra) (0x[0-9a-f]+)", head.group(0)))
named = [(k, regs[k]) for k in ("mepc", "ra") if k in regs and regs[k] != "0x00000000"]
stack = bt.group(1).split() if bt else []
lines = addr2line(elf, [a for _, a in named] + stack)
for (k, a), l in zip(named, lines):
    print(f"  {k:4} {a}  {l}")
print("  stack, return-address candidates (top first; a few can be stale):")
for a, l in zip(stack, lines[len(named):]):
    print(f"       {a}  {l}")
