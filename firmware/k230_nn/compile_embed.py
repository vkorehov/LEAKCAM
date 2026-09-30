#!/usr/bin/env python3
"""x86-64 container side: data/embed_b1.tflite (make_embed.py) -> the int16 kmodel
firmware/k230_capture/leakcam_change.kmodel, then the change distance of the check pairs on the
nncase simulator vs TensorFlow.

Input is the reduced luma frame leakcam_wake already holds (uint8 NHWC 1x240x320x1); the net's
own Rescaling layer maps it to [-1, 1], so nncase preprocess only casts uint8 -> float. Output is
the float 1x15x20x192 feature map. int16 PTQ (activations and weights): uint8 moves the distance
of a pair by up to 0.1, int16 by 0.001.
Usage: ./run_x86.sh compile_embed.py      (compile about 1 min under qemu, simulation a few more)
"""
import os
import time

import numpy as np
import nncase

HERE = os.path.dirname(os.path.abspath(__file__))
D = os.path.join(HERE, "data")
KMODEL = os.path.join(HERE, "..", "k230_capture", "leakcam_change.kmodel")


def distance(fr, fc):
    a = fr / (np.linalg.norm(fr, axis=-1, keepdims=True) + 1e-6)
    b = fc / (np.linalg.norm(fc, axis=-1, keepdims=True) + 1e-6)
    return float((1 - (a * b).sum(-1)).max())


def main():
    dump = os.path.join(HERE, "build")
    os.makedirs(dump, exist_ok=True)
    cal = np.load(os.path.join(D, "embed_calib.npy"))           # N,240,320,1 uint8

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
    comp = nncase.Compiler(co)
    comp.import_tflite(open(os.path.join(D, "embed_b1.tflite"), "rb").read(), nncase.ImportOptions())
    ptq = nncase.PTQTensorOptions()
    ptq.samples_count = 20
    ptq.calibrate_method = "Kld"
    ptq.quant_type = "int16"
    ptq.w_quant_type = "int16"
    ptq.set_tensor_data([[c[None]] for c in cal[:20]])
    comp.use_ptq(ptq)
    t0 = time.time()
    comp.compile()
    km = comp.gencode_tobytes()
    open(KMODEL, "wb").write(km)
    print(f"leakcam_change.kmodel {len(km)} bytes, compile {time.time() - t0:.0f} s")

    sim = nncase.Simulator()
    sim.load_model(km)

    def feat(x):
        sim.set_input_tensor(0, nncase.RuntimeTensor.from_numpy(x[None].copy()))
        sim.run()
        return sim.get_output_tensor(0).to_numpy().reshape(15, 20, 192)
    pairs = np.load(os.path.join(D, "embed_pairs.npy"))         # P,2,240,320,1
    tfd = np.load(os.path.join(D, "embed_pairs_tfdist.npy"))
    kinds = open(os.path.join(D, "embed_pairs_kinds.txt")).read().split()
    worst = 0.0
    for k, p, d in zip(kinds, pairs, tfd):
        km_d = distance(feat(p[0]), feat(p[1]))
        worst = max(worst, abs(km_d - d))
        print(f"pair {k:8s} change distance TF {d:.3f}  kmodel {km_d:.3f}")
    print(f"worst |kmodel - TF| {worst:.4f}")


if __name__ == "__main__":
    main()
