#!/usr/bin/env python3
"""Rebuild a file from one or more captures of RT-Thread msh `hexdump <file>` on the K230 console.

    unhexdump.py <size> <out> <capture> [<capture> ...]

The console drops characters now and then; a damaged line is rejected when its hex does not match its own ASCII
column (2026-10-21: a line with dropped characters still parsed as 16 bytes, so the column check is needed).
Lines from several captures are merged; captures that disagree on a line are reported. Exit 1 if a line is
missing in every capture: dump again and add that capture. Only ONE reader on the tty while dumping (a second
`cat` splits the data between them).
"""
import re
import sys

PAT = re.compile(r'^([0-9a-f]{8})  ((?:[0-9a-f]{2} |   ){8}) ((?:[0-9a-f]{2} |   ){8}) \| (.*)$')


def ascii_col(bs):
    return ''.join(chr(b) if 0x20 <= b <= 0x7e else '.' for b in bs)


def parse(fn, size):
    lines = {}
    for line in open(fn, 'rb').read().decode('latin1').replace('\r', '').split('\n'):
        m = PAT.match(line)
        if not m:
            continue
        off = int(m.group(1), 16)
        bs = bytes.fromhex((m.group(2) + m.group(3)).replace(' ', ''))
        if off % 16 or off >= size or len(bs) != min(16, size - off) or m.group(4) != ascii_col(bs):
            continue
        lines[off] = bs
    return lines


def main():
    size, out, caps = int(sys.argv[1]), sys.argv[2], sys.argv[3:]
    parsed = [parse(c, size) for c in caps]
    data, missing, disagree = bytearray(size), [], 0
    for off in range(0, size, 16):
        seen = [p[off] for p in parsed if off in p]
        if not seen:
            missing.append(off)
            continue
        disagree += any(s != seen[0] for s in seen)
        data[off:off + len(seen[0])] = seen[0]
    open(out, 'wb').write(data)
    for c, p in zip(caps, parsed):
        print(f"{c}: {len(p)} of {(size + 15) // 16} lines good")
    print(f"disagreeing lines {disagree}, missing in all {len(missing)}" +
          (": " + ", ".join(hex(o) for o in missing[:20]) if missing else ""))
    sys.exit(1 if missing or disagree else 0)


if __name__ == '__main__':
    main()
