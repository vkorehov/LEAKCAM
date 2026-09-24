#!/usr/bin/env python3
"""x86-64 container side: embed_b1.tflite (luma 240x320 -> 15x20x192 features, frozen
ImageNet MobileNetV2-0.35) -> .kmodel with uint8 luma input, PTQ on leakcam luma frames,
then simulator features vs TFLite features (cosine per cell).

The input is the raw uint8 luma plane (what ai2d produces from the NV12 Y plane); the net's
own Rescaling layer maps it to [-1, 1], so nncase preprocess only casts uint8 -> float.
Usage: ./run_x86.sh compile_embed.py
"""
import argparse
import os
import time

import numpy as np
import nncase

HERE = os.path.dirname(os.path.abspath(__file__))
D = os.path.join(HERE, "data")


def feat_metric(fr, fc):
    a = fr / (np.linalg.norm(fr, axis=-1, keepdims=True) + 1e-6)
    b = fc / (np.linalg.norm(fc, axis=-1, keepdims=True) + 1e-6)
    return float((1 - (a * b).sum(-1)).max())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quant", choices=["uint8", "int16"], default="uint8")
    ap.add_argument("--reuse", action="store_true")
    a = ap.parse_args()
    tag = "embed_u8" if a.quant == "uint8" else "embed_u8_i16"
    dump = os.path.join(HERE, "build", tag)
    os.makedirs(dump, exist_ok=True)
    cal = np.load(os.path.join(D, "embed_calib.npy"))           # N,240,320,1 uint8
    xt = np.load(os.path.join(D, "embed_test_x.npy"))
    ref = np.load(os.path.join(D, "embed_test_ref.npy"))        # 4,15,20,192

    co = nncase.CompileOptions()
    co.target = "k230"
    co.dump_dir = dump
    co.input_shape = [1, 240, 320, 1]
    co.preprocess = True
    co.input_type = "uint8"
    co.input_range = [0, 255]
    co.input_layout = "NHWC"
    co.model_layout = "NHWC"
    co.mean = [0]
    co.std = [1]
    co.swapRB = False
    kpath = os.path.join(dump, tag + ".kmodel")
    if a.reuse:
        return simulate(open(kpath, "rb").read(), xt, ref)
    comp = nncase.Compiler(co)
    comp.import_tflite(open(os.path.join(D, "embed_b1.tflite"), "rb").read(), nncase.ImportOptions())
    ptq = nncase.PTQTensorOptions()
    ptq.samples_count = 20
    ptq.calibrate_method = "Kld"
    if a.quant == "int16":
        ptq.quant_type = "int16"
        ptq.w_quant_type = "int16"
    ptq.set_tensor_data([[c[None]] for c in cal[:20]])
    comp.use_ptq(ptq)
    t0 = time.time()
    comp.compile()
    km = comp.gencode_tobytes()
    open(kpath, "wb").write(km)
    print(f"{tag}.kmodel {len(km)} bytes, compile {time.time() - t0:.0f} s")
    simulate(km, xt, ref)


def simulate(km, xt, ref):

    sim = nncase.Simulator()
    sim.load_model(km)
    for x, r in zip(xt, ref):
        sim.set_input_tensor(0, nncase.RuntimeTensor.from_numpy(x[None].copy()))
        sim.run()
        f = sim.get_output_tensor(0).to_numpy().reshape(r.shape)
        a = f / (np.linalg.norm(f, axis=-1, keepdims=True) + 1e-6)
        b = r / (np.linalg.norm(r, axis=-1, keepdims=True) + 1e-6)
        cos = (a * b).sum(-1)
        print(f"sim vs tflite: per-cell cosine mean {cos.mean():.4f} min {cos.min():.4f}; "
              f"rel L2 err {np.linalg.norm(f - r) / np.linalg.norm(r):.3f}")

    # the decision that matters: change distance between two frames, sim vs TF
    pairs = np.load(os.path.join(D, "embed_pairs.npy"))         # P,2,240,320,1
    tfd = np.load(os.path.join(D, "embed_pairs_tfdist.npy"))
    kinds = open(os.path.join(D, "embed_pairs_kinds.txt")).read().split()

    def feat(x):
        sim.set_input_tensor(0, nncase.RuntimeTensor.from_numpy(x[None].copy()))
        sim.run()
        return sim.get_output_tensor(0).to_numpy().reshape(ref.shape[1:])
    for k, p, d in zip(kinds, pairs, tfd):
        print(f"pair {k:8s} change distance TF {d:.3f}  kmodel {feat_metric(feat(p[0]), feat(p[1])):.3f}")


if __name__ == "__main__":
    main()
