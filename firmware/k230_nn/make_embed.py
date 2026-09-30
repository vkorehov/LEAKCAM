#!/usr/bin/env python3
"""Host side of the change net: builds data/embed_b1.tflite and the pairs compile_embed.py checks
the kmodel against, and sets the change threshold.

The net is MobileNetV2-0.35 with its ImageNet weights, cut at block_13_expand_relu (stride 16,
192 channels): luma 240x320x1 in, a 15x20x192 feature map out. No training. The change distance
between two frames is the max over the 300 cells of (1 - cosine) of their feature vectors, so a
small puddle, 1-3 cells, is not averaged away.

Pairs, from ~/leakcam (read-only), prepared as imgdiff_reduce() prepares the K230 frame (area
resize to 320x240, 3x3 blur):
  light    a real dry frame vs itself with gain 0.6..1.5, gamma 0.7..1.4 and sensor noise: same
  realdry  real dry captures minutes apart (AGC drift): same
  wet      nano-banana leak/dissolved vs its dry base, also under a lighting change: different
  wipe     a wiped floor vs its base: a real change, reported
The threshold is the largest distance of any "same" pair: no false alarm from lighting.

  /home/vkorehovs/watermeter/.venv/bin/python make_embed.py      (TF 2.21, OpenCV; about 3 min)
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
LAYER = "block_13_expand_relu"


def luma(path):
    im = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
    if im is None:
        raise SystemExit(f"unreadable {path}")
    return cv2.blur(cv2.resize(im, (320, 240), interpolation=cv2.INTER_AREA), (3, 3))


def photometric(g, rng):
    gain, gamma = rng.uniform(0.6, 1.5), rng.uniform(0.7, 1.4)
    x = ((g / 255.0) ** gamma) * gain * 255 + np.random.default_rng(rng.randint(0, 1 << 30)).normal(0, 3, g.shape)
    return np.clip(x, 0, 255).astype(np.uint8)


def build_embed():
    inp = tf.keras.Input((240, 320, 1), batch_size=1)
    x = tf.keras.layers.Concatenate()([inp, inp, inp])          # luma -> the RGB the weights expect
    x = tf.keras.layers.Rescaling(1 / 127.5, offset=-1.0)(x)     # MobileNetV2 preprocessing
    base = tf.keras.applications.MobileNetV2(input_shape=(240, 320, 3), alpha=0.35,
                                             include_top=False, weights="imagenet")
    feat = tf.keras.Model(base.input, base.get_layer(LAYER).output)
    return tf.keras.Model(inp, feat(x))


def distance(fr, fc):
    a = fr / (np.linalg.norm(fr, axis=-1, keepdims=True) + 1e-6)
    b = fc / (np.linalg.norm(fc, axis=-1, keepdims=True) + 1e-6)
    return float((1 - (a * b).sum(-1)).max())


def main():
    os.makedirs(D, exist_ok=True)
    rng = random.Random(3)
    model = build_embed()
    open(os.path.join(D, "embed_b1.tflite"), "wb").write(tf.lite.TFLiteConverter.from_keras_model(model).convert())

    pairs = []                                      # (kind, ref, cur)
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
            if kind == "wet":
                pairs.append((kk + "+light", ref, photometric(cur, rng)))

    feats = lambda g: model.predict(g[None, ..., None].astype(np.float32), verbose=0)[0]
    dist = [distance(feats(r), feats(c)) for _, r, c in pairs]
    same = [d for (k, _, _), d in zip(pairs, dist) if k in ("light", "realdry")]
    thr = float(np.ceil(max(same) * 100) / 100)
    print(f"{'kind':12s} {'n':>3s} {'min':>6s} {'p50':>6s} {'max':>6s}  above {thr:.2f}")
    for k in sorted({k for k, _, _ in pairs}):
        v = np.array([d for (kk, _, _), d in zip(pairs, dist) if kk == k])
        print(f"{k:12s} {len(v):3d} {v.min():6.3f} {np.median(v):6.3f} {v.max():6.3f}  {np.mean(v > thr):.0%}")
    print(f"threshold {thr:.2f}: CHANGE_THRESHOLD in firmware/k230_capture/change.h")
    open(os.path.join(D, "threshold.txt"), "w").write(f"{thr:.2f}\n")

    # for nncase: calibration frames, and a spread of pairs with their TF distance
    np.save(os.path.join(D, "embed_calib.npy"),
            np.stack([p[1] for p in pairs[::max(1, len(pairs) // 40)]][:40])[..., None].astype(np.uint8))
    pick = [i for k in ("light", "realdry", "wet", "small", "wipe")
            for i in [j for j, p in enumerate(pairs) if p[0] == k][:3]]
    np.save(os.path.join(D, "embed_pairs.npy"),
            np.stack([np.stack([pairs[i][1], pairs[i][2]]) for i in pick])[..., None].astype(np.uint8))
    np.save(os.path.join(D, "embed_pairs_tfdist.npy"), np.array([dist[i] for i in pick]))
    open(os.path.join(D, "embed_pairs_kinds.txt"), "w").write("\n".join(pairs[i][0] for i in pick))


if __name__ == "__main__":
    main()
