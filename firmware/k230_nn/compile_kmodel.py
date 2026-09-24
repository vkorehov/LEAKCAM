#!/usr/bin/env python3
"""x86-64 container side: TFLite -> K230 .kmodel with PTQ, then nncase simulator vs TF.

Runs under python:3.11-slim (amd64) with nncase==2.11.0 nncase-kpu==2.11.0 and the
.NET 7 runtime (see README.md "Commands"). Inputs from prep_host.py in ./data.

  python3 compile_kmodel.py --variant f32    float32 NHWC input, stack fed as-is
  python3 compile_kmodel.py --variant u8     uint8 NHWC input (stack*255), nncase
                                             preprocess divides by 255 on the KPU
"""
import argparse
import os
import time

import numpy as np
import nncase

HERE = os.path.dirname(os.path.abspath(__file__))
D = os.path.join(HERE, "data")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", choices=["f32", "u8"], default="f32")
    ap.add_argument("--quant", choices=["uint8", "int16"], default="uint8",
                    help="PTQ weights/activations type")
    ap.add_argument("--ncal", type=int, default=47)
    ap.add_argument("--reuse", action="store_true", help="skip compiling, simulate the existing kmodel")
    a = ap.parse_args()
    tag = f"leak_{a.variant}_{a.quant}"
    dump = os.path.join(HERE, "build", tag)
    os.makedirs(dump, exist_ok=True)

    xcal = np.load(os.path.join(D, "calib.npy"))[: a.ncal]      # N,90,160,4 float [0,1]
    xt = np.load(os.path.join(D, "test.npy"))
    ref = np.load(os.path.join(D, "ref_tf.npy"))                 # sev, inop (keras), sev, inop (tflite)
    names = open(os.path.join(D, "test_names.txt")).read().split()

    co = nncase.CompileOptions()
    co.target = "k230"
    co.dump_ir = False
    co.dump_asm = False
    co.dump_dir = dump
    co.input_shape = [1, 90, 160, 4]
    if a.variant == "u8":
        co.preprocess = True
        co.input_type = "uint8"
        co.input_range = [0, 1]            # uint8 0..255 dequantised to 0..1 = the TF input
        co.input_layout = "NHWC"
        co.model_layout = "NHWC"
        co.mean = [0, 0, 0, 0]
        co.std = [1, 1, 1, 1]
        co.swapRB = False
        to_in = lambda x: np.round(x * 255).astype(np.uint8)
    else:
        co.preprocess = False
        to_in = lambda x: x.astype(np.float32)

    kpath = os.path.join(dump, f"{tag}.kmodel")
    if a.reuse:
        return simulate(open(kpath, "rb").read(), xt, ref, names, to_in, dump)
    comp = nncase.Compiler(co)
    comp.import_tflite(open(os.path.join(D, "leak_b1.tflite"), "rb").read(), nncase.ImportOptions())

    ptq = nncase.PTQTensorOptions()
    ptq.samples_count = len(xcal)
    ptq.calibrate_method = "Kld"   # NoClip / Kld; Kld is the SDK examples' choice for CNNs
    if a.quant == "int16":
        ptq.quant_type = "int16"
        ptq.w_quant_type = "int16"
    ptq.set_tensor_data([[to_in(x[None])] for x in xcal])
    comp.use_ptq(ptq)

    t0 = time.time()
    comp.compile()
    km = comp.gencode_tobytes()
    open(kpath, "wb").write(km)
    print(f"{tag}: kmodel {len(km)} bytes, compile {time.time() - t0:.0f} s")
    simulate(km, xt, ref, names, to_in, dump)


def simulate(km, xt, ref, names, to_in, dump):

    sim = nncase.Simulator()
    sim.load_model(km)
    for i in range(sim.inputs_size):
        print("input", i, sim.get_input_desc(i).dtype)
    out = []
    for x in xt:
        sim.set_input_tensor(0, nncase.RuntimeTensor.from_numpy(to_in(x[None])))
        sim.run()
        out.append([float(sim.get_output_tensor(k).to_numpy().ravel()[0]) for k in range(sim.outputs_size)])
    out = np.array(out)
    # kmodel output order follows the tflite graph outputs; align with the keras order by fit
    if np.abs(out[:, 0] - ref[:, 0]).mean() > np.abs(out[:, 1] - ref[:, 0]).mean():
        out = out[:, ::-1]
    np.save(os.path.join(dump, "sim_out.npy"), out)
    ds, di = np.abs(out[:, 0] - ref[:, 0]), np.abs(out[:, 1] - ref[:, 1])
    print(f"{'sample':52s} {'TF sev':>7s} {'sim sev':>7s} {'TF inop':>7s} {'sim inop':>8s}")
    for n, r, o in zip(names, ref, out):
        print(f"{n[:52]:52s} {r[0]:7.3f} {o[0]:7.3f} {r[1]:7.3f} {o[1]:8.3f}")
    alarm = 0.15
    agree = ((out[:, 0] > alarm) == (ref[:, 0] > alarm)).mean()
    iagree = ((out[:, 1] > 0.7) == (ref[:, 1] > 0.7)).mean()
    print(f"|sev diff| mean {ds.mean():.4f} max {ds.max():.4f}; |inop diff| mean {di.mean():.4f} "
          f"max {di.max():.4f}; alarm@0.15 agreement {agree:.1%}; inop@0.7 agreement {iagree:.1%}")


if __name__ == "__main__":
    main()
