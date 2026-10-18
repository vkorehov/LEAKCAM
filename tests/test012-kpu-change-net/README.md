# test012: KPU, change net (leakcam_change.kmodel), 2026-10-18 - open

The change net ([firmware/k230_nn](../../firmware/k230_nn/README.md)) on the KPU against the 15 check pairs of
`make_embed.py` (`data/embed_pairs.npy`, TensorFlow distances 0.005 .. 0.48). Temporary test programs in the SDK
copy only (not in the repo): `leakcam_kputest` (pairs through `change_distance()`), `leakcam_kpurun` (any kmodel,
fixed input, time and NaN count).

| Step | Result | |
|---|---|---|
| First run | hung in the first inference: the runtime waits in `poll()` (no timeout) for the KPU interrupt; the KPU was never powered | fixed |
| Cause | `pm_core_init()` looks up `regulator_cpu`/`regulator_kpu`, which only exist with `CONFIG_RT_USING_REGULATOR` (dummy regulators; every Canaan board sets it); it failed before its last step, KPU clock + power on | defconfig |
| After the fix | power manager runs (`thermal_detect_thread`); AI clock PLL3/2 = 800 MHz, AI power domain on (registers read via `/dev/mem`) | pass |
| Canaan `face_detection_320.kmodel` (fixed input) | **2.0 ms** per run, no error | pass (KPU works) |
| `leakcam_change.kmodel` | **6.26 s** per run, 57570 of 57600 output values NaN; every distance read 0.0000 | **fail** |
| Same kmodel in the nncase 2.11 simulator | matched TensorFlow (k230_nn README); compiler and runtime both 2.11.0 | |

**Open.** The KPU, its clock and power and the runtime work; our int16 kmodel does not run correctly on the chip.
Next: a uint8 / int16-activation-int8-weight variant to see whether int16 quantisation is the cause, and the repo
kmodel through the simulator again.

Found on the way: `change_distance()` returned 0 ("light only, sleep") for NaN features, because NaN fails every
comparison: a broken net would have hidden every change. Fixed in `change.cc`: a non-finite distance returns -1
(net failed -> the change is reported).
