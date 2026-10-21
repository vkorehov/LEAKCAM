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
| `leakcam_change.kmodel` (int16), stream-loaded | distances right (0.1787 / 0.3680 vs TF 0.179 / 0.368) but **6.3 s per inference** | slow |
| KPU registers watched during one int16 inference | busy 34 ms, idle until 2.9 s, then short bursts with 0.2-0.8 s idle gaps, done at 6.4 s; timeout register 200000 (set by the runtime) | CPU layers |
| int16 IR (`dump_ir`, after the quantise pass) | 40 convolutions left in **float32** (plain `Conv2D`, `f32` weights): run on the CPU; only 9 layer groups lowered to the KPU (`GNNE*`) | cause of the 6 s |
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

**int8 chosen** (user, 2026-10-19). All 219 `make_embed.py` pairs on the KPU (4.4 ms per check):

| Pair kind | n | int8 on the KPU, above 0.43 | TensorFlow, above 0.44 |
|---|---|---|---|
| light | 24 | max 0.400, 0 % | max 0.431, 0 % |
| realdry | 5 | max 0.425, 0 % | max 0.348, 0 % |
| wet | 72 | p50 0.432, 51 % | p50 0.441, 50 % |
| wet + light | 72 | p50 0.419, 40 % | p50 0.428, 42 % |
| small / small + light | 8 / 8 | 25 % / 12 % | 25 % / 12 % |
| wipe | 30 | p50 0.435, 53 % | p50 0.422, 43 % |

int8 vs TensorFlow: mean 0.031, max 0.15 per pair, correlation 0.955. Threshold from the int8 distances:
**0.43** (`CHANGE_THRESHOLD`). The int8 kmodel is the factory `leakcam_change.kmodel` (`compile_embed.py` builds it);
the product image with it boots and the agent ends its session normally.

**Pass.**



## Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: KPU power-up and kmodel load time (at boot or at first use in the session).

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: extra current during inference vs idle; energy per change check (4.4 ms per check).

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
- **int16 net takes 6.3 s although correct** -> nncase's int16 PTQ for k230 left 40 convolutions in float32, which
  run on the CPU; found by watching the KPU status/PC during a run (long idle gaps) and counting ops in the IR
  (`dump_ir`, `6_QuantizePass/.../End_main.il`) -> int8, whole net on the KPU.
- **219 pairs (34 MB) did not fit the 50 MB /sdcard image** -> genimage packed it without an error -> test image
  built with a larger /sdcard in the SDK copy only; the product layout stays 50 MB.
- **nncase compile aborts at once (`CompileOptionsSetInputShape`)** -> `input_shape` not set -> set it (and
  `swapRB`) as `compile_embed.py` does.
- **A crashed qemu compile left its container running for 20 min** -> `timeout` around `docker run` kills only the
  client -> name the container, `docker stop <name>`; run with `-w /tmp` so a qemu core dump stays out of the repo.
- **K230 USB boot device never reached the VM** -> plug order (bug-002): the CH340 attached first -> CH340 out,
  USB-C 1 in, then `pt_on`, `pt_run`; CH340 back after the popup.
