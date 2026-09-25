#!/usr/bin/env python3
"""Reduce an Altium ODB++ export to what an antenna simulation needs, for CST import.

  strip_odb_for_cst.py MIFA_cst_full.tgz MIFA_cst_stripped.tgz

Kept: every pour (surface) on every layer and net; traces and pads of GND and the antenna feed
net; vias of GND, the feed and every net that has a pour, so the pours stay connected as on the
board; board outline, stack-up, dielectric and solder mask layers; components and netlist.
Removed: other traces and pads (signal and power traces, BGA and component pads), signal vias,
and the silkscreen and paste features.

ODB++ refers to features by their 0-based index in each layer's features file (eda/data
"FID C|H <layer> <index>"), so every reference is renumbered, and references to removed
features are dropped. The fab outputs are not touched: this file only feeds the simulation.
"""
import os, re, sys, shutil, tarfile, tempfile, collections

FEED_NETS = {'GND', 'NetANT1_1'}
EMPTY_TYPES = {'SILK_SCREEN', 'SOLDER_PASTE'}
RECORD = re.compile(r'^(L|P|A|T|B|S) ')


def read_matrix(root):
    layers, cur = {}, {}
    for l in open(os.path.join(root, 'matrix', 'matrix'), encoding='latin1'):
        l = l.strip()
        if l.startswith('LAYER'):
            cur = {}
        elif '=' in l:
            k, v = l.split('=', 1); cur[k] = v
        elif l == '}' and 'NAME' in cur:
            layers[cur['NAME'].lower()] = cur.get('TYPE', '')
    return layers


def split_features(path):
    """header lines, list of records (each a list of lines) and the file's own line end"""
    text = open(path, encoding='latin1', newline='').read()
    lines = text.split('\n')
    if text.endswith('\n'):
        lines.pop()                              # the empty piece after the last line end
    head, recs, i = [], [], 0
    while i < len(lines) and not RECORD.match(lines[i]):
        head.append(lines[i]); i += 1
    while i < len(lines):
        l = lines[i]
        if RECORD.match(l):
            rec = [l]; i += 1
            if l.startswith('S '):
                while i < len(lines) and lines[i].strip() != 'SE':
                    rec.append(lines[i]); i += 1
                if i < len(lines):
                    rec.append(lines[i]); i += 1
            recs.append(rec)
        else:
            if l.strip():
                recs.append(('RAW', l))          # anything unexpected is kept as is
            i += 1
    return head, recs, text.endswith('\n')


def main():
    src, dst = sys.argv[1], sys.argv[2]
    tmp = tempfile.mkdtemp()
    tarfile.open(src).extractall(tmp)
    root = os.path.join(tmp, os.listdir(tmp)[0])
    types = read_matrix(root)
    step = os.path.join(root, 'steps', os.listdir(os.path.join(root, 'steps'))[0])
    eda = os.path.join(step, 'eda', 'data')
    eda_lines = open(eda, encoding='latin1', newline='').read().split('\n')

    # layer order used by FID records, and feature -> net
    lyr = next(l.split()[1:] for l in eda_lines if l.startswith('LYR '))
    feat_net, net = {}, None
    for l in eda_lines:
        if l.startswith('NET '):
            net = l.split()[1]
        elif l.startswith('FID ') and net is not None:
            p = l.split()          # CRLF files: split() also drops the trailing \r
            feat_net[(lyr[int(p[2])], int(p[3]))] = net

    pour_nets = set()
    parsed = {}
    for name, t in types.items():
        f = os.path.join(step, 'layers', name, 'features')
        if os.path.exists(f):
            parsed[name] = split_features(f)
            if t == 'SIGNAL':
                for idx, r in enumerate(x for x in parsed[name][1] if not isinstance(x, tuple)):
                    if r[0].startswith('S ') and feat_net.get((name, idx)) not in (None, '$NONE$'):
                        pour_nets.add(feat_net[(name, idx)])
    via_nets = FEED_NETS | pour_nets

    remap, stats = {}, collections.OrderedDict()
    for name, (head, recs, final_eol) in parsed.items():
        t = types.get(name, '')
        feats = [r for r in recs if not isinstance(r, tuple)]
        keep_idx = []
        for idx, r in enumerate(feats):
            kind = r[0][0]; n = feat_net.get((name, idx))
            if t in EMPTY_TYPES:
                keep = False
            elif t == 'SIGNAL':
                keep = kind == 'S' or n in FEED_NETS
            elif t == 'DRILL':
                keep = n in via_nets
            else:
                keep = True                    # dielectric, mask, keep-out, components
            if keep:
                keep_idx.append(idx)
        remap[name] = {old: new for new, old in enumerate(keep_idx)}
        kept = set(keep_idx)
        out = head[:]
        fi = 0
        for r in recs:
            if isinstance(r, tuple):
                out.append(r[1]); continue
            if fi in kept:
                out.extend(r)
            fi += 1
        open(os.path.join(step, 'layers', name, 'features'), 'w', encoding='latin1', newline='').write(
            '\n'.join(out) + ('\n' if final_eol else ''))
        if len(feats) != len(keep_idx):
            stats[name] = (len(feats), len(keep_idx))

    # renumber the netlist's feature references, drop the ones to removed features
    out, dropped = [], 0
    for l in eda_lines:
        if l.startswith('FID '):
            p = l.split()
            ln = lyr[int(p[2])]
            if ln in remap:
                new = remap[ln].get(int(p[3]))
                if new is None:
                    dropped += 1; continue
                p[3] = str(new); l = ' '.join(p) + ('\r' if l.endswith('\r') else '')
        out.append(l)
    open(eda, 'w', encoding='latin1', newline='').write('\n'.join(out))

    with tarfile.open(dst, 'w:gz') as tf:
        tf.add(root, arcname=os.path.basename(root))
    shutil.rmtree(tmp)
    print('nets whose vias are kept: %s' % ', '.join(sorted(via_nets)))
    for name, (a, b) in stats.items():
        print('  %-40s %5d -> %5d features' % (name, a, b))
    print('netlist feature references dropped: %d' % dropped)


if __name__ == '__main__':
    main()
