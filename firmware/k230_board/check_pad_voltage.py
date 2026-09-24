#!/usr/bin/env python3
"""
Check the IO bank voltage (MSC) that a U-Boot/SPL binary or a .dtb programs into the K230 pads,
against LEAKCAM's hardware: every VDDIO bank is on 3V3, only IO0/IO1 (BOOT0/1) are 1.8 V.

    check_pad_voltage.py u-boot-spl.bin u-boot.bin board.dtb ...

For a binary, every embedded FDT is found by its magic and decoded. Pads come from each
"pinctrl-single,pins" property of the iomux node (pairs: register offset, value; pad = offset/4).
MSC is bit 9 of the pad value: 0 = 3.3 V, 1 = 1.8 V (K230_MSC_3V3 / K230_MSC_1V8).
Exit 1 if any 3.3 V pad would be put into 1.8 V mode (or the reverse on IO0/IO1).

Why this exists: Canaan's prebuilt K230 EVB SPI-NAND SPL/U-Boot set most banks, including the
NAND bank IO14-IO25, to 1.8 V mode; Canaan's own device trees warn a wrong bank voltage "will
damage the chip". This tool rejected them and accepts the LEAKCAM build.
"""
import struct
import sys

FDT_MAGIC = 0xD00DFEED


def fdts(blob):
    i = 0
    while True:
        i = blob.find(struct.pack(">I", FDT_MAGIC), i)
        if i < 0:
            return
        size = struct.unpack(">I", blob[i + 4:i + 8])[0]
        if 64 < size < 1 << 20 and i + size <= len(blob):
            yield blob[i:i + size]
        i += 4


def props(fdt):
    """(node path, property name, value bytes) for every property"""
    off_struct, off_strings = struct.unpack(">II", fdt[8:16])
    strings = fdt[off_strings:]
    p, path = off_struct, []
    while p + 4 <= len(fdt):
        tok = struct.unpack(">I", fdt[p:p + 4])[0]
        p += 4
        if tok == 1:                                   # BEGIN_NODE
            end = fdt.index(b"\0", p)
            path.append(fdt[p:end].decode("latin1"))
            p = (end + 4) & ~3
        elif tok == 2:                                 # END_NODE
            path.pop()
        elif tok == 3:                                 # PROP
            ln, nameoff = struct.unpack(">II", fdt[p:p + 8])
            p += 8
            name = strings[nameoff:strings.index(b"\0", nameoff)].decode("latin1")
            yield "/".join(path), name, fdt[p:p + ln]
            p = (p + ln + 3) & ~3
        elif tok == 9:                                 # END
            return


def expected_msc(pad):
    return 1 if pad in (0, 1) else 0


def check(fdt, label):
    bad = seen = 0
    for path, name, val in props(fdt):
        if name != "pinctrl-single,pins" or "pmu" in path:
            continue                                   # PMU pads (IO64+) have no MSC field
        words = struct.unpack(">%dI" % (len(val) // 4), val)
        for off, v in zip(words[0::2], words[1::2]):
            pad = off // 4
            if pad > 63:
                continue
            seen += 1
            msc = (v >> 9) & 1
            if msc != expected_msc(pad):
                bad += 1
                print("  %s: IO%-2d = 0x%04x -> %s mode, board needs %s" % (
                    label, pad, v, "1.8 V" if msc else "3.3 V",
                    "1.8 V" if expected_msc(pad) else "3.3 V"))
    return seen, bad


def main(paths):
    rc = 0
    for path in paths:
        blob = open(path, "rb").read()
        trees = list(fdts(blob))
        if not trees:
            print("%s: no device tree found" % path)
            rc = 1
            continue
        for n, t in enumerate(trees):
            seen, bad = check(t, "%s[fdt %d]" % (path, n))
            state = "OK" if bad == 0 else "WRONG BANK VOLTAGE on %d pads" % bad
            print("%s[fdt %d]: %d pads checked, %s" % (path, n, seen, state))
            if bad or not seen:
                rc = 1
    return rc


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1:]))
