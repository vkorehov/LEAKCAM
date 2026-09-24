#!/usr/bin/env python3
"""Host side (aarch64, system TF): no training, read-only on ~/leakcam.

1. leak.keras -> leak_b1.tflite with a FIXED batch of 1 (nncase needs static shapes;
   the shipped leak.tflite has batch -1).
2. calibration stacks (lib.preprocess.stack, exactly the training preprocessing) from
   a spread of cohorts: wet, dry, wipe, real dry transitions, failure frames.
3. test stacks (real-wet held-out sequence, real dry transitions, synthetic, inop) and
   the TF-Keras and TFLite reference outputs [severity, P(inop)].

Outputs go to ./data/ next to this script. Usage: python3 prep_host.py
"""
import os
import random
import sys

import numpy as np

LEAK = os.path.expanduser("~/leakcam")
TRAIN = os.path.join(LEAK, "step5-leak-train")
sys.path.insert(0, TRAIN)
os.chdir(TRAIN)

import cv2  # noqa: E402
import tensorflow as tf  # noqa: E402

from lib.layout import base_of, samples  # noqa: E402
from lib.preprocess import real_dry_frames, stack  # noqa: E402
import model_train_seg as mts  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
os.makedirs(OUT, exist_ok=True)
APPROVED = os.path.join(TRAIN, "input-approved", "images")
AUG = os.path.join(TRAIN, "input-augmented", "images")
REAL = os.path.join(TRAIN, "input-real", "images")
CAP = os.path.join(TRAIN, "input-captures")


def rd(p):
    im = cv2.imread(p)
    if im is None:
        raise SystemExit(f"unreadable {p}")
    return cv2.cvtColor(im, cv2.COLOR_BGR2RGB)


def pair(d, n):
    b = base_of(d, n)
    if not b:
        return None
    return stack(rd(os.path.join(d, n)), rd(os.path.realpath(os.path.join(d, b))))


def pick(d, prefix, k, rng):
    names = [n for n in samples(d) if n.startswith(prefix) and base_of(d, n)]
    return [(d, n) for n in rng.sample(names, min(k, len(names)))]


def main():
    rng = random.Random(1)
    model = mts.load_infer(os.path.join(TRAIN, "output", "leak.keras"))

    # 1. fixed-batch TFLite, float32 (nncase does its own PTQ)
    inp = tf.keras.Input((mts.H, mts.W, mts.C), batch_size=1)
    fixed = tf.keras.Model(inp, model(inp))
    tfl = tf.lite.TFLiteConverter.from_keras_model(fixed).convert()
    open(os.path.join(OUT, "leak_b1.tflite"), "wb").write(tfl)
    print(f"leak_b1.tflite {len(tfl)} bytes")

    # 2. calibration: 48 stacks across cohorts (none from the test picks below)
    cal = (pick(APPROVED, "nano-banana-dissolved", 10, rng) + pick(APPROVED, "nano-banana-leak", 8, rng)
           + pick(APPROVED, "nano-banana-rewet", 8, rng) + pick(APPROVED, "nano-banana-dry", 6, rng)
           + pick(APPROVED, "nano-banana-wipe", 4, rng) + pick(AUG, "real-fail-", 6, rng))
    xs = [pair(d, n) for d, n in cal]
    dry = real_dry_frames(CAP)
    for i in rng.sample(range(3, len(dry)), min(6, len(dry) - 3)):
        xs.append(stack(rd(dry[i]), rd(dry[i - 3])))
    xcal = np.stack(xs).astype(np.float32)
    np.save(os.path.join(OUT, "calib.npy"), xcal)
    print("calib", xcal.shape, "range", xcal.min(), xcal.max())

    # 3. test set: real-wet every 10th frame, 6 real dry transitions, synthetic + inop
    calnames = {n for _, n in cal}
    wet = [n for n in samples(REAL) if n.startswith("real-wet-")][::10]
    test = [(REAL, n) for n in wet]
    for pre, k in (("nano-banana-dissolved", 3), ("nano-banana-leak", 3), ("nano-banana-dry", 3)):
        test += [t for t in pick(APPROVED, pre, k + 3, random.Random(7)) if t[1] not in calnames][:k]
    for pre in ("real-fail-dark", "real-fail-banding", "real-fail-blur", "real-fail-jitter"):
        test += [t for t in pick(AUG, pre, 3, random.Random(7)) if t[1] not in calnames][:1]
    names = [n for _, n in test]
    xt = [pair(d, n) for d, n in test]
    for j, i in enumerate(random.Random(9).sample(range(3, len(dry)), min(6, len(dry) - 3))):
        xt.append(stack(rd(dry[i]), rd(dry[i - 3])))
        names.append("realdry-" + os.path.basename(dry[i]))
    xt = np.stack(xt).astype(np.float32)
    np.save(os.path.join(OUT, "test.npy"), xt)
    open(os.path.join(OUT, "test_names.txt"), "w").write("\n".join(names) + "\n")

    sev, inop = mts.predict_sev_inop(model, xt)
    it = tf.lite.Interpreter(os.path.join(OUT, "leak_b1.tflite"))
    it.allocate_tensors()
    ind, outd = it.get_input_details()[0], it.get_output_details()
    tl = []
    for x in xt:
        it.set_tensor(ind["index"], x[None])
        it.invoke()
        tl.append([float(it.get_tensor(o["index"]).ravel()[0]) for o in outd])
    tl = np.array(tl)
    # TFLite output order is not guaranteed: match columns to Keras by correlation-free abs diff
    if np.abs(tl[:, 0] - sev).mean() > np.abs(tl[:, 1] - sev).mean():
        tl = tl[:, ::-1]
    print("tflite output names", [o["name"] for o in outd])
    ref = np.stack([sev, inop, tl[:, 0], tl[:, 1]], 1)
    np.save(os.path.join(OUT, "ref_tf.npy"), ref)
    print(f"keras vs tflite max |d| sev {np.abs(ref[:, 0] - ref[:, 2]).max():.2e} "
          f"inop {np.abs(ref[:, 1] - ref[:, 3]).max():.2e}")
    for n, r in zip(names, ref):
        print(f"{n[:60]:60s} sev {r[0]:.3f} inop {r[1]:.3f}")


if __name__ == "__main__":
    main()
