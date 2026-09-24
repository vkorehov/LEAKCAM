#!/usr/bin/env python3
"""Can a FROZEN ImageNet MobileNetV2-0.35 feature map tell a real change from a lighting change
better than gain-normalised pixel MAD (the imgdiff.c metric)? No training: this measures the
untrained-for-this-task baseline the change net would start from.

Pairs (all from ~/leakcam, read-only):
  light   real dry frame vs itself with a photometric change (gain 0.6..1.5, gamma 0.7..1.4,
          sensor noise) -> MUST read "same"
  realdry real dry capture t vs t-3 (minutes apart, AGC drift)  -> "same"
  wipe    nano-banana-wipe vs its dry base (floor wiped: a real, but benign, change)
  wet     nano-banana-{leak,dissolved} vs dry base -> "different"
  small   wet pairs whose severity <= 0.25 (drops, small puddles) -> "different", the hard case

Metrics per pair, both taken as the MAX over a spatial grid (a small puddle must not be averaged
away), on luma at 320x240 like the K230 path:
  mad     imgdiff-style: gain-normalised |ref - gain*cur|, 3x3 blur, block mean over a 16x12 grid
  feat    1 - cosine similarity of the stride-16 feature vectors (20x15 cells), max over cells

Also writes data/embed_b1.tflite: luma 240x320x1 -> stride-16 feature map, for nncase.
Usage: KERAS_HOME=$PWD/keras_home python3 change_embed_poc.py
"""
import os
import random
import sys

import numpy as np

LEAK = os.path.expanduser("~/leakcam")
TRAIN = os.path.join(LEAK, "step5-leak-train")
sys.path.insert(0, TRAIN)
import cv2  # noqa: E402
import tensorflow as tf  # noqa: E402
from lib.layout import base_of, labels_from_name, samples  # noqa: E402
from lib.preprocess import real_dry_frames  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
D = os.path.join(HERE, "data")
APPROVED = os.path.join(TRAIN, "input-approved", "images")
CAP = os.path.join(TRAIN, "input-captures")
LAYER = "block_13_expand_relu"    # stride 16, 192 channels at alpha 0.35


def luma(path):
    im = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
    if im is None:
        raise SystemExit(f"unreadable {path}")
    return cv2.resize(im, (320, 240), interpolation=cv2.INTER_AREA)


def photometric(g, rng):
    gain, gamma = rng.uniform(0.6, 1.5), rng.uniform(0.7, 1.4)
    x = ((g / 255.0) ** gamma) * gain * 255 + np.random.default_rng(rng.randint(0, 1 << 30)).normal(0, 3, g.shape)
    return np.clip(x, 0, 255).astype(np.uint8)


def mad_metric(ref, cur):
    """imgdiff.c without the fisheye circle mask: gain-normalise, 3x3 blur, 16x12 block mean."""
    r = cv2.blur(ref.astype(np.float32), (3, 3))
    c = cv2.blur(cur.astype(np.float32), (3, 3))
    gain = np.clip(r.mean() / max(c.mean(), 1.0), 0.5, 2.0)
    d = np.abs(r - gain * c)
    return d.reshape(12, 20, 16, 20).mean(axis=(1, 3)).max()


def build_embed():
    inp = tf.keras.Input((240, 320, 1), batch_size=1)
    x = tf.keras.layers.Concatenate()([inp, inp, inp])          # luma -> the RGB the weights expect
    x = tf.keras.layers.Rescaling(1 / 127.5, offset=-1.0)(x)     # MobileNetV2 preprocessing
    base = tf.keras.applications.MobileNetV2(input_shape=(240, 320, 3), alpha=0.35,
                                             include_top=False, weights="imagenet")
    feat = tf.keras.Model(base.input, base.get_layer(LAYER).output)
    return tf.keras.Model(inp, feat(x))


def feat_metric(fr, fc):
    a = fr / (np.linalg.norm(fr, axis=-1, keepdims=True) + 1e-6)
    b = fc / (np.linalg.norm(fc, axis=-1, keepdims=True) + 1e-6)
    return float((1 - (a * b).sum(-1)).max())


def main():
    rng = random.Random(3)
    model = build_embed()
    macs = 0
    for l in model.layers[-1].layers:
        if isinstance(l, (tf.keras.layers.Conv2D, tf.keras.layers.DepthwiseConv2D)):
            o, k = l.output.shape, l.kernel_size
            cin = l.input.shape[-1]
            macs += o[1] * o[2] * o[3] * k[0] * k[1] * (1 if isinstance(l, tf.keras.layers.DepthwiseConv2D) else cin)
    print(f"embed: params {model.count_params()}, ~{macs / 1e6:.0f} MMAC at 320x240, "
          f"out {model.output.shape}")
    tfl = tf.lite.TFLiteConverter.from_keras_model(model).convert()
    open(os.path.join(D, "embed_b1.tflite"), "wb").write(tfl)

    pairs = []   # (kind, ref_luma, cur_luma)
    dry = real_dry_frames(CAP)
    for p in dry:
        g = luma(p)
        for _ in range(3):
            pairs.append(("light", g, photometric(g, rng)))
    for a, b in zip(dry[3:], dry[:-3]):
        pairs.append(("realdry", luma(b), luma(a)))
    names = samples(APPROVED)
    for pre, kind, k in (("nano-banana-wipe", "wipe", 30), ("nano-banana-leak", "wet", 40),
                         ("nano-banana-dissolved", "wet", 40)):
        cand = [n for n in names if n.startswith(pre) and base_of(APPROVED, n)]
        for n in rng.sample(cand, min(k, len(cand))):
            ref = luma(os.path.realpath(os.path.join(APPROVED, base_of(APPROVED, n))))
            cur = luma(os.path.join(APPROVED, n))
            sev = labels_from_name(n)[0]
            kk = kind if kind != "wet" or sev is None or sev > 0.25 else "small"
            pairs.append((kk, ref, cur))
            if kind == "wet":   # same wet pair under a lighting change: must still read "different"
                pairs.append((kk + "+light", ref, photometric(cur, rng)))

    # calibration images for nncase: luma frames the embed net will see
    cal = np.stack([p[1] for p in pairs[::max(1, len(pairs) // 40)]][:40])[..., None]
    np.save(os.path.join(D, "embed_calib.npy"), cal.astype(np.uint8))

    rows = []
    for kind, r, c in pairs:
        fr = model.predict(r[None, ..., None].astype(np.float32), verbose=0)[0]
        fc = model.predict(c[None, ..., None].astype(np.float32), verbose=0)[0]
        rows.append((kind, mad_metric(r, c), feat_metric(fr, fc)))
    np.save(os.path.join(D, "embed_test_ref.npy"),
            np.stack([pairs[0][1], pairs[0][2]])[..., None].astype(np.uint8))
    kinds = sorted({k for k, _, _ in rows})
    print(f"{'kind':12s} {'n':>3s}  {'MAD p50':>8s} {'MAD min':>8s} {'MAD max':>8s}   "
          f"{'feat p50':>8s} {'feat min':>8s} {'feat max':>8s}")
    for k in kinds:
        m = np.array([r[1] for r in rows if r[0] == k])
        f = np.array([r[2] for r in rows if r[0] == k])
        print(f"{k:12s} {len(m):3d}  {np.median(m):8.2f} {m.min():8.2f} {m.max():8.2f}   "
              f"{np.median(f):8.3f} {f.min():8.3f} {f.max():8.3f}")

    # separability: threshold at the max over "same" pairs (zero false alarms on lighting)
    same = [r for r in rows if r[0] in ("light", "realdry")]
    for idx, name in ((1, "MAD"), (2, "feat")):
        thr = max(r[idx] for r in same)
        for k in ("wet", "wet+light", "small", "small+light", "wipe"):
            v = [r[idx] for r in rows if r[0] == k]
            if v:
                print(f"{name:4s} thr {thr:7.3f} (max over same)  {k:12s} detected "
                      f"{np.mean(np.array(v) > thr):.0%} of {len(v)}")
    np.save(os.path.join(D, "embed_rows.npy"),
            np.array([(kinds.index(k), m, f) for k, m, f in rows]))


if __name__ == "__main__":
    main()
