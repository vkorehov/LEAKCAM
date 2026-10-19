# test012: KPU, change net (leakcam_change.kmodel), 2026-10-18/19

The change net ([firmware/k230_nn](../../firmware/k230_nn/README.md)) on the KPU against the 15 check pairs of
`make_embed.py` (`data/embed_pairs.npy` with TensorFlow's distances). Temporary test programs, in the SDK copy only
(not in the repo): `leakcam_kputest` (the pairs through `change_distance()`), `leakcam_kpurun` (any kmodel, fixed
input), `leakcam_kmin` (a one-layer model with known output), Canaan's `yolov8_image.elf`.

## Results

| Step | Result | |
|---|---|---|
| First run | hung in the first inference: the runtime waits in `poll()` (no timeout) for the KPU interrupt; the KPU was never powered | fixed |
| Cause | `pm_core_init()` looks up `regulator_cpu`/`regulator_kpu`, which exist only with `CONFIG_RT_USING_REGULATOR` (dummy regulators; every Canaan board sets it); it failed before switching the KPU clock and power on | defconfig |
| Canaan YOLOv8 example (`best.kmodel`, `test.jpg`) | banana 0.89, orange 0.86, apple 0.81: the KPU, its clock (PLL3/2 = 800 MHz), power and the runtime work | pass |
| Minimal model (one 1x1 conv, 8x8x4), loaded from a memory buffer | output all 0, 0.05 ms | fail |
| Same, loaded through a stream | A sum 191.5132, B 127.9264, A again identical: bit-exact to the nncase simulator | pass |
| `leakcam_change.kmodel` (int16), stream-loaded | distances right (0.1787 / 0.3680 vs TF 0.179 / 0.368) but **6.3 s per inference** (the same 6.26 s as before the fix: looks like the KPU's timeout ends each run) | slow |
| uint8 variant (217 KB) | 0.5 ms per inference, 23 ms per pair; vs TF max 0.060, mean 0.033; "same" pairs max 0.388 | pass |
| int8 variant (211 KB) | 0.5 ms per inference, 23 ms per pair; vs TF max 0.078, mean 0.025; "same" pairs max 0.290 | pass |

Distances per pair (threshold 0.44):

| Kind | TF | uint8 | int8 |
|---|---|---|---|
| light (same) | 0.179 0.368 0.172 | 0.140 0.388 0.222 | 0.146 0.290 0.165 |
| realdry (same) | 0.156 0.178 0.005 | 0.128 0.144 0.015 | 0.110 0.153 0.038 |
| wet (change) | 0.410 0.426 0.437 | 0.452 0.366 0.458 | 0.425 0.430 0.423 |
| small (change) | 0.191 0.085 0.208 | 0.150 0.135 0.160 | 0.172 0.087 0.173 |
| wipe (change) | 0.448 0.417 0.480 | 0.444 0.382 0.492 | 0.437 0.397 0.452 |

**KPU: pass.** Change net: the shipped int16 kmodel is correct but takes 6.3 s per inference (12.6 s per check),
too slow for a wake. The 8-bit variants take 0.5 ms. **Open:** which one ships; an 8-bit net needs its threshold
set from its own distances over the full pair set (`make_embed.py` / `compile_embed.py` on the simulator), not
TensorFlow's 0.44; or the int16 slowness gets found (no "done" interrupt?).

## Obstacles

- **First inference hangs** -> KPU never powered: `pm_core_init()` fails without `CONFIG_RT_USING_REGULATOR` ->
  added to `k230d_leakcam_defconfig` (dummy regulators; the core rails are fixed regulators).
- **NaN, constant or zero outputs; int16 run 6.3 s** -> `change.cc` loaded the kmodel from a memory span: the
  runtime then runs the caller's heap pages in place and the KPU (physical addresses) reads garbage. Canaan's
  samples load from a stream, where the runtime copies the model into its own buffers -> `change_load()` loads
  through a read-only `streambuf` over the bytes. Found with a one-layer model whose output is known exactly.
- **NaN distance read as "light only"** -> NaN fails every comparison, so a broken net hid every change ->
  `change_distance()` returns -1 (net failed, change reported) on a non-finite distance.
- **Canaan's YOLOv8 example finds nothing** -> it decodes 3 classes, its `best.kmodel` has 80 (output 1x84x2100) ->
  class count from the output shape (test copy only).
- **Cache invalidate of the output** -> tried, no effect; not the cause.
- **nncase compile aborts at once (`CompileOptionsSetInputShape`)** -> `input_shape` not set -> set it (and
  `swapRB`) as `compile_embed.py` does.
- **A crashed qemu compile left its container running for 20 min** -> `timeout` around `docker run` kills only the
  client -> name the container, `docker stop <name>`; run with `-w /tmp` so a qemu core dump stays out of the repo.
- **K230 USB boot device never reached the VM** -> plug order (bug-002): the CH340 attached first -> CH340 out,
  USB-C 1 in, then `pt_on`, `pt_run`; CH340 back after the popup.
