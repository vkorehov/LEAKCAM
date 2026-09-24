# LEAKCAM neural networks on the K230D KPU (RT-Smart): plan and proof of concept

Date: 2026-09-24. No hardware was used; the weights are still the Pi-camera ones, so no kmodel is committed. Everything under "measured" ran on the host: TensorFlow
2.21 on aarch64, plus nncase 2.11.0 and its K230 simulator in an x86-64 container.
`~/leakcam`, `~/k230_rtos_sdk` and `~/k230d-hw/LEAKCAM` were only read. Nothing was retrained.

## Summary

| Question | Answer |
|---|---|
| Can the existing leakcam TF model run on the KPU? | **Yes, it compiles.** `leak.keras` -> fixed-batch TFLite -> nncase 2.11 PTQ -> **275 KB kmodel**. On 28 test pairs the simulator matches TF within 0.008 severity (mean 0.0016). Alarm and INOPERATIONAL decisions agree 100 %. |
| Can the weights be used on LEAKCAM as they are? | **No.** They were trained on the Pi's USB camera (640x480 RGB, perspective view, a wall band for exposure correction). LEAKCAM has two 222° fisheye OV5647 with LED/IR light. The architecture and toolchain carry over; the weights need a LEAKCAM dataset and a retrain. |
| Compiler on this aarch64 host? | Not natively. PyPI has **no linux-aarch64 wheel** for `nncase` or `nncase-kpu` 2.11.0. It runs in an x86-64 container, but only under **qemu-user 11.1**: the host's qemu 8.2.2 crashes starting .NET 7, which the compiler needs. `run_x86.sh` has the workaround. |
| Change network ("meaningfully different")? | A siamese feature-map comparison on MobileNetV2-0.35 (stride 16, 20x15 cells). Frozen ImageNet weights, untrained for this task, already beat the imgdiff metric: **69 % vs 47 %** of wet pairs caught with zero false alarms on lighting changes. That is not enough to ship, so train it (see "Change network"). It compiles to a 217 KB kmodel (uint8) or 372 KB (int16). uint8 PTQ distorts the change distances; int16 is exact. |
| LED control and defocus | Classic signals on the imgdiff 320x240 frame, CPU only (`k230/imgqual.c`). The sharpness ratio against the reference separates the inoperational `blur` cohort (max 0.20) from survivable `softblur`/`jitter`/wet (min 0.53). A neural net is not needed for this. |
| Device code | `k230/leak_nn.cc` runs quality check, then change net, then leak net in image mode. It **cross-compiles and links** against the SDK's nncase 2.11 runtime, OpenCV and MPP libs (13 MB static `leak_nn.elf`). Not run on hardware. |

## 1. What ~/leakcam's model is

Sources: `~/leakcam/DESIGN.md`, `step5-leak-train/model_train_seg.py`, `lib/preprocess.py`,
`step5-leak-train/model_dataset.py`, `lib/layout.py`.

- **Task**: fixed-camera under-floor leak detection. The input is always a pair: the current
  frame and a dry baseline. The baseline is the `<name>-base.jpg` sibling symlink; `-mask.jpg`
  is the per-pixel wet mask. There are no CSVs: labels are in the filename (`-sevNNN`,
  `-dryingNNN`) and read with `lib.layout.labels_from_name()` / `base_of()`.
- **Outputs**: `leak.keras` gives `[severity 0..1, P(inoperational)]`. Severity is computed
  from a wetness map: `0.7*sqrt(area) + 0.29*depth*area`. It is not regressed directly.
  Inoperational threshold: 0.7 (`INOP_THRESHOLD`). Alarm: 0.15.
- **Input**: `lib.preprocess.stack(cur_rgb, base_rgb)` gives 90x160x4 float32 in [0,1],
  computed from 640x480 frames with the top 120 rows (wall band) cut off:
  - ch0: CLAHE(cur), clip 2.5, 8x8 tiles
  - ch1: |CLAHE diff|
  - ch2: CLAHE(base)
  - ch3: signed darkening corrected by the wall band's brightness change: 0.5 = unchanged,
    lower = darker (wetter)

  All four are resized with INTER_AREA.
- **Architecture**: a 3-level U-Net (16/32/64/64 channels) with a sigmoid wetmap. The depth
  head is a Dense on the pooled bottleneck. The inop head is its own 3-conv strided branch on
  the input, with GAP and GMP, then Dense 16, then sigmoid.
- **Size**: 215,643 parameters (842 KB fp32). **342 MMAC** per inference at 90x160, counting
  conv and dense layers only.
- **TFLite ops**: CONV_2D, MAX_POOL_2D, RESIZE_NEAREST_NEIGHBOR, CONCATENATION, MEAN,
  REDUCE_MAX, FULLY_CONNECTED, LOGISTIC, SQRT, MUL, ADD, MINIMUM, MAXIMUM, RELU. nncase imports
  all of them.
- **Measured results** (DESIGN.md, 2026-09-03), on the held-out real drying curve: MAE 0.067,
  correlation 0.99, 97.4 % detection, 0 false alarms. Inop recall is 85 % at 0.7.
- **Trained files**: `step5-leak-train/output/leak.keras`, `leak.tflite` (batch -1, which
  nncase cannot take) and `leak-wetmap.keras`.
- **Data**:
  - step3 approved set: 8832 files (dissolved 1029, rewet 1076, leak 427, dry 232, wipe 180)
  - step4 procedural failure cohorts: 20 each of dark, deadrows, deadcols, banding and blur
    (inoperational) and of rotate, shift, jitter and softblur (survivable)
  - step1 held-out real-wet sequence: 100 frames
  - `captures/` holds 21 frames; only 8 survive the wet-window filter (`real_dry_frames`)

## 2. K230 deployment path

**Versions.**

- SDK runtime: `src/rtsmart/libs/nncase/riscv64/nncase/include/nncase/version.h` =
  `NNCASE_VERSION "2.11.0"`.
- `src/rtsmart/libs/kmodel/version` = `v2.11.0` (repo `nncase_kmodels_v2_11`).
- Compiler and runtime versions must match, so use `nncase==2.11.0` and `nncase-kpu==2.11.0`.
  The SDK README still says 2.9.0; that is stale.

**Wheels.**

- PyPI 2.11.0 `nncase` wheels exist only for manylinux x86_64, macOS arm64 and win_amd64
  (cp39 to cp313).
- `nncase-kpu` exists only for manylinux x86_64.
- Nothing is built for linux-aarch64, so compilation happens in an x86-64 container:
  - `python:3.11-slim` amd64 image, 189 MB
  - wheels installed with `pip --target pyenv`, 168 MB
  - .NET 7.0.20 x64 runtime, 71 MB. The compiler is a .NET 7 app; see
    `Nncase.Compiler.runtimeconfig.json`.
- **The host's binfmt qemu 8.2.2 (Ubuntu) cannot run it**. It hangs, or dies with "QEMU
  internal SIGSEGV" in CLR start-up. Debian's qemu-user 11.1.1 static `qemu-x86_64` works:
  unpacked into `qemu11/` (14 MB) and used as the container entrypoint, so the host binfmt
  config is not touched. It also needs `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1` (no ICU in
  the slim image) and `DOTNET_EnableWriteXorExecute=0`.
- The simulator shells out to `nncase.simulator.k230.sc`, a plain x86 binary that the host's
  qemu 8.2 runs fine, so it has to be on PATH.

  Speed under emulation: leak kmodel compile about 4 min; simulation about 20 s per inference.

**Runtime API on RT-Smart.** This is the pattern in
`src/rtsmart/examples/ai/usage_kpu/yolov8_run_camera/main.cc` and `ai_demo/common_files/ai_base.cc`:

```cpp
interpreter ip; ip.load_model(ifs);
auto t = host_runtime_tensor::create(ip.input_desc(0).datatype, ip.input_shape(0), hrt::pool_shared);
ip.input_tensor(0, t);        // same for outputs
// camera path: wrap the VICAP frame's phys addr as a runtime_tensor, then
ai2d_builder b(in_shape, out_shape, dtype, crop, shift, pad, resize, affine); b.build_schedule();
b.invoke(frame_tensor, ip.input_tensor(0).unwrap());   // resize/crop/pad on the 2D engine
ip.run();  ip.output_tensor(i) -> to_host() -> map(map_read)
```

- Link order: `rvv Nncase.Runtime.Native nncase.rt_modules.k230 functional_k230 sys ... atomic`,
  then opencv (`opencv_imgproc` has CLAHE).
- `ai2d_format` supports YUV420_NV12/NV21/I420, NCHW and RGB_packed input.
- The SDK has two demos close to what LEAKCAM needs:
  - `ai_demo/anomaly_det`: PatchCore. Its kmodel is 144 KB, plus a `memory.bin` feature bank.
  - `ai_demo/self_learning`: register and compare feature vectors.

**Enabling it.** `configs/k230d_rtos_leakcam_defconfig` enables no AI examples. The leakcam
app would link the prebuilt `libs/nncase` and `libs/opencv` itself, as
`k230/build_leak_nn.sh` does. Nothing in the SDK needs to change.

**Memory.** On K230D (`boards/Kconfig.memory_static`), the 128 MiB is split into RT-Smart
58 MiB (heap 8 MiB) and **MMZ 70 MiB** at 0x3A00000. The cameras take about 26 MB of it
(firmware README).

| Item | Size |
|---|---|
| leak kmodel | 275 KB |
| its input, f32 | 230 KB |
| its largest activation | about 0.5 MB |
| embed kmodel | 217 or 372 KB |
| its input | 77 KB |
| its feature output, f32 | 230 KB |

**Everything NN-related fits in about 2 MB.** Memory is not a constraint.

**Latency** (not verified, estimated, no hardware). Canaan quotes about 6 TOPS INT8
equivalent for the K230 KPU. The SDK runs yolov8n (about 4 GMAC) at camera frame rate.
Scaling from that:

- leak net (342 MMAC): about 3-8 ms
- embed net at 320x240 (67 MMAC): about 1-3 ms in uint8, maybe 2-4x that in int16
- CPU preprocessing: probably the larger cost. That is CLAHE on two 640x480 frames plus
  resizes on the C908 core, around 10-40 ms.

`leak_nn` prints each of these timings. At one capture per wake, none of it matters next to
boot and AE settling.

## 3. Proof: leakcam model -> kmodel -> simulator vs TF

`prep_host.py` does this, read-only:

- exports `leak.keras` with batch 1 to `data/leak_b1.tflite` (882 KB). Keras vs TFLite max
  |d| is 3e-6.
- builds 47 calibration stacks with the exact training preprocessing: dissolved 10, leak 8,
  rewet 8, dry 6, wipe 4, real-fail 6, real dry transitions 5.
- builds 28 test stacks:
  - 10 real-wet frames (every 10th of the held-out curve)
  - 3 dissolved, 3 leak, 3 dry
  - 4 camera-failure frames (dark, banding, blur, jitter)
  - 5 real dry transitions

`compile_kmodel.py` does PTQ with KLD calibration and uint8 weights and activations, then
compares with TF:

| Variant | kmodel | Input | abs(sev diff) mean / max | abs(inop diff) mean / max | alarm@0.15 / inop@0.7 agreement |
|---|---|---|---|---|---|
| `leak_f32_uint8` | 274,808 B | float32 1x90x160x4 | **0.0016 / 0.0083** | 0.019 / 0.069 | 100 % / 100 % |
| `leak_u8_uint8` | 273,400 B | uint8 stack*255, KPU dequantises | 0.0082 / 0.0725 | 0.018 / 0.053 | 100 % / 100 % |

Per-sample tables are in `sim_f32.log` and `build_u8.log`. Use the **float32-input** variant,
because the uint8 input loses the darkening channel's sub-level resolution. The worst case
was real-wet frame 071: 0.234 vs 0.307.

`check_order.py` confirms the kmodel **output 0 = P(inop), output 1 = severity**. The inop
sigmoid can read about -0.03 after PTQ, so clamp it.

## 4. Change network: "is this meaningfully different?"

**What imgdiff.c does today.**

1. Reduce 1280x960 to 320x240 with a 4x4 box filter, then a 3x3 blur and the image-circle mask.
2. Normalise with a single global gain.
3. Take the block MAD over a 16x12 grid; a block over 12 levels counts as changed.

A lighting change that is not a pure gain (LED level vs ambient, AGC gamma, IR vs white,
shadows) moves many blocks.

**Measured baseline, no training** (`change_embed_poc.py`, `data/embed_rows.npy`). The feature
extractor is MobileNetV2-0.35 with ImageNet weights, cut at `block_13_expand_relu` (stride 16,
192 channels, 20x15 cells at 320x240). It has 98.5 k parameters and **67 MMAC**. The input is
luma replicated to 3 channels. The score is the maximum over cells of (1 - cosine). "Same"
pairs are real dry frames with gain 0.6-1.5, gamma 0.7-1.4 and noise added, plus real dry
transitions. The threshold is set to the maximum over the "same" pairs, i.e. zero false
alarms from lighting:

| Pair kind (n) | imgdiff-style MAD detected | Frozen features detected |
|---|---|---|
| wet (72) | 47 % | **69 %** |
| wet + lighting change (72) | 49 % | **67 %** |
| small wet, sev ≤ 0.25 (8) | 0 % | 25 % |
| wipe, a real benign change (30) | 3 % | 57 % |

The features are more lighting-invariant than pixel MAD. Untrained, they still miss a third
of the wet pairs and most small puddles. **So the network has to be trained**, and that is
cheap:

- **Architecture** (KPU-friendly, all standard convs):
  - MobileNetV2-0.35 up to stride 16, then a 1x1 conv to a 64-d L2-normalised embedding per
    cell. This is the "embed" kmodel, luma 240x320 in.
  - A tiny "decision" head on `|e_ref - e_cur|` and `e_ref * e_cur`: two 1x1 convs, then a
    per-cell sigmoid. Output is a 20x15 change map plus its max and area.
  - The reference embedding is computed once per baseline refresh and stored on the NAND:
    15x20x64 int8 = 19 KB per camera. Each wake runs only the current frame through the
    backbone, plus the head.
- **Training data**: reuse leakcam's pairing. The `-mask.jpg` siblings supply per-cell labels,
  so changed = mask coverage above a few % of the cell.
  - "same": real dry transitions, the photometric jitter from `stack()`'s `jitter_rng`,
    survivable cohorts (`jitter`, `softblur`, small `rotate`/`shift`), plus LED-level sweeps
    and IR/white swaps once LEAKCAM captures exist.
  - "different": wet/dissolved/rewet pairs and wipes. A wipe **is** a meaningful change here;
    whether it is a leak is the second net's job.
  - Loss: per-cell BCE, balanced as in `model_dataset.balance()`.
  - Size: roughly a 10-20 min run, not the leak net's 1 h (not verified).
- **Quantisation**: measured with frozen weights on 10 pairs (`sim_embed_u8.log`,
  `build_embed_i16.log`):
  - **uint8 PTQ** gives per-cell cosine of 0.95 to TF (min 0.78). Lighting-pair distances rise
    from 0.14-0.25 to 0.23-0.32, overlapping the smallest wet pair.
  - **int16 PTQ** gives cosine 1.000 and matches TF to 0.001.

  Use int16 for this net (372 KB), or retrain with a 64-d projection and re-measure uint8.
  The KPU's int16 throughput cost is not verified; at 67 MMAC it should not matter.

**Why feature maps and not one global embedding**: a small puddle changes 1-3 of 300 cells. A
global pooled vector averages that away, as the leak net's own history shows ("integrating
many local decisions"). The max and area over cells keeps imgdiff's block semantics.

## 5. Image quality: LED level, defocus, fog and dirt (`k230_capture/imgqual.[ch]`)

These are computed on the 320x240 frame that `imgdiff_reduce()` already produces, inside the
image circle. One histogram pass and one Sobel/Laplacian pass, about 0.3 M operations:

| Signal | Meaning | Use |
|---|---|---|
| p01 / p50 / p99, mean | exposure | LED loop |
| sat_frac (≥ 250), dark_frac (≤ 8) | clipping, dead LED | LED loop, "too dark" verdict |
| contrast (p99 - p01) / 255 | fog and dirt flatten it | ratio vs reference |
| tenengrad (mean Sobel², above the noise floor), lapvar, edge_frac | sharpness | ratio vs reference |

**Measured** on leakcam's step4 cohorts against their bases (`data/imgqual_ratios.txt`,
columns: kind, tenengrad ratio, lapvar ratio, contrast ratio, p99, p01, sat, mean, LED step):

| Cohort (20 each, wet 30) | Tenengrad ratio min / median / max |
|---|---|
| **blur** (inoperational) | 0.029 / 0.105 / **0.197** |
| softblur (survivable) | **0.538** / 0.705 / 0.909 |
| jitter | 0.607 / 1.004 / 1.446 |
| wet | 0.529 / 0.982 / 1.225 |
| dark | 0.000 / 0.000 / 0.001 |
| deadrows / deadcols / banding | 1.2 - 6.4 (artifacts add edges) |

A threshold of **0.35 × reference** (`imgqual_judge`) separates blur from everything
survivable on this data. It is a ratio because M12 lenses are fixed-focus: absolute sharpness
depends on the scene and the light. It detects defocus from a knock, fogging, water and dirt
on the lens. Water and dust on the lens look the same (DESIGN.md), so they share one state.
Contrast ratio < 0.5 is the fog check. **The thresholds must be re-measured on LEAKCAM
fisheye frames** (not verified).

**LED control** (`imgqual_led_step`). This follows the illumination rule already in place:
the scene must be lit to 2 m, so start at maximum current and only back off.

- `sat_frac > 1 %`: ×0.7.
- `p99 < 150` with nothing clipping: ×1.4, capped at 100 %.
- Judge exposure first, then sharpness, because a dark frame also reads as "blurred".

Two caveats:

1. The OV5647 auto exposure (AE) fights this loop. Either lock exposure and gain while
   stepping the LED, or treat the AE's reported gain as part of the measurement.
   not verified: what the K230 ISP API exposes here.
2. leakcam's own frames have p99 ≈ 254 (that camera clips), so their LED-step output means
   nothing.

No neural network is needed for LED control. The leak net's inop head covers dark, banding
and blur as a learned second opinion, and gets LEAKCAM failure cohorts when it is retrained.

## 6. Recommended pipeline per wake, per camera

```
capture (LEDs at PWM p, AE settled or locked)            existing leakcam_capture
 |
 +-> imgqual on 320x240 --- too dark / clipped -> LED step, recapture (max 2 retries)
 |                      \-- sharpness < 0.35x ref or contrast < 0.5x ref -> INOPERATIONAL (lens)
 |
 +-> imgdiff vs base AND vs last (existing, now a GATE with LOW thresholds: its job is recall;
 |     false "changed" only costs a few ms of KPU)
 |        no change -> done (history/refs as today)
 v
 embed.kmodel (current frame) + head vs stored reference embedding  -> change map 20x15
 |        no meaningful change -> done (optionally refresh 'last', never 'base' while elevated)
 v
 leak.kmodel (current vs dry BASE, 4-ch stack on CPU) -> P(inop) first, then severity
 |        P(inop) > 0.7 -> INOPERATIONAL; severity > 0.15 on N consecutive wakes -> ALARM
 v
 JSON line + history keyframe of changed blocks (existing)
```

Rules carried over from DESIGN.md:

- Compare against the dry **base**, not only the last frame, or a slow seep gets absorbed.
- Refresh the base only after N dry-scored wakes, and never while severity is elevated.
- Run the leak net unconditionally every K-th wake as well, so a gate miss is bounded in time.

## 7. What must change before this is real

1. **LEAKCAM dataset**: fisheye captures from both cameras, with white and IR, at several LED
   levels. Run step2's synthetic wet edits on them (same gates) and step4's failure cohorts.
2. **Leak net preprocessing for fisheye**. The wall band (top 120 rows) has no counterpart.
   Replace the exposure reference with:
   - a region known never to be wet (the upper hemisphere of the up-facing lens, or the
     ceiling in the horizontal camera), or
   - imgdiff's global gain.

   Use the image-circle mask as the "floor". Input can stay at 90x160 per camera, or use
   120x160 for 4:3.
3. **Retrain** the leak net about 1 h (not done here, as instructed), plus the change net.
4. Re-measure every threshold on LEAKCAM frames: imgqual ratios, change threshold, severity
   alarm, INOP_THRESHOLD.
5. On hardware:
   - run `leak_nn.elf`
   - check `--ai2d` with single-channel NCHW (not verified; the SDK examples only use 3
     channels, and the default path uses `cv::resize`)
   - measure latency and energy per wake

## Files

Paths are relative to `firmware/k230_nn/`. The image-quality code lives with the capture code in `firmware/k230_capture/` (`imgqual.[ch]`, `test/test_imgqual.c`). Generated files (`data/`, `build/`, `pyenv/`, `dotnet/`, `qemu11/`, `keras_home/`, `*.log`, `k230/leak_nn.elf`) are not in git: the commands below recreate them.

| File | What |
|---|---|
| `prep_host.py` | host: fixed-batch TFLite, calibration and test stacks, TF/TFLite reference outputs |
| `compile_kmodel.py` | x86 container: leak TFLite -> kmodel (`--variant f32/u8`, `--quant uint8/int16`, `--reuse`), simulator vs TF |
| `change_embed_poc.py` | host: frozen-MobileNetV2 change metric vs imgdiff MAD on leakcam pairs; writes `data/embed_b1.tflite` |
| `compile_embed.py` | x86 container: embed TFLite -> kmodel (`--quant`), simulator vs TF, pair distances |
| `check_order.py` | kmodel output order |
| `run_x86.sh` | runs a script in amd64 `python:3.11-slim` under `qemu11/.../qemu-x86_64` with `pyenv/` and `dotnet/` |
| `build/leak_f32_uint8/leak_f32_uint8.kmodel` | **recommended leak kmodel (current weights, Pi camera domain)** |
| `build/leak_u8_uint8/leak_u8_uint8.kmodel` | uint8-input variant |
| `build/embed_u8/embed_u8.kmodel`, `build/embed_u8_i16/embed_u8_i16.kmodel` | frozen-feature change backbone, uint8 / int16 |
| `k230/leak_nn.cc` | RT-Smart image-mode PoC: imgqual -> imgdiff -> embed -> leak, with timings |
| `../k230_capture/imgqual.[ch]` | image-quality signals, LED step, verdict |
| `../k230_capture/test/test_imgqual.c` | host tool used for the table in section 5 (`make -C firmware/k230_capture test/test_imgqual`) |
| `k230/build_leak_nn.sh` | cross-compile `leak_nn.elf` in a throw-away container from `k230-rtos-sdk-build:arm64-x86tc` (SDK mounted read-only, no make) |
| `k230/leak_nn.elf` | the linked RISC-V static binary (13 MB, not stripped) |
| `data/` | tflite files, calibration and test arrays, reference outputs, imgqual tables |
| `*.log` | the measured outputs quoted above |

## Commands

```bash
cd firmware/k230_nn
# one-time x86 toolchain (about 260 MB)
docker pull --platform linux/amd64 python:3.11-slim
docker run --rm --platform linux/amd64 -v $PWD:/w python:3.11-slim \
  pip install -q --target /w/pyenv nncase==2.11.0 nncase-kpu==2.11.0 'numpy<2'
mkdir -p dotnet && curl -sSL https://builds.dotnet.microsoft.com/dotnet/Runtime/7.0.20/dotnet-runtime-7.0.20-linux-x64.tar.gz | tar xz -C dotnet
curl -sSLO http://deb.debian.org/debian/pool/main/q/qemu/qemu-user_11.1.1+ds-1_arm64.deb
mkdir -p qemu11 && dpkg-deb --fsys-tarfile qemu-user_11.1.1+ds-1_arm64.deb | tar -x -C qemu11 --wildcards '*/qemu-x86_64' && rm qemu-user_*.deb

PYTHONDONTWRITEBYTECODE=1 python3 prep_host.py                  # host, about 1 min
./run_x86.sh compile_kmodel.py --variant f32                     # about 4 min compile + about 10 min simulation
KERAS_HOME=$PWD/keras_home PYTHONDONTWRITEBYTECODE=1 python3 change_embed_poc.py
./run_x86.sh compile_embed.py --quant int16
k230/build_leak_nn.sh                                           # -> k230/leak_nn.elf

# on the board (not verified): copy the elf and kmodels to /sdcard/app, frames from leakcam_capture --pgm
./leak_nn.elf embed_u8_i16.kmodel leak_f32_uint8.kmodel cam0-ref.pgm cam0-now.pgm [--ai2d] [--thr 0.42]
```

## Open risks

- **Domain gap** (main risk): the leak weights come from a different camera, lens and
  lighting. The kmodel proves the toolchain, not the detection quality on LEAKCAM.
- The wall-band exposure correction has no fisheye counterpart. It needs a replacement and
  a retrain (section 7.2).
- The change network is only proposed. Its numbers are for frozen ImageNet weights on 8 real
  dry frames and synthetic wet pairs. The "same" set is small: leakcam has only 8 usable real dry
  captures (21 in `captures/`, the rest fall inside wet windows).
- uint8 PTQ of the change backbone distorts distances enough to matter. int16 fixes it in the
  simulator; KPU int16 speed is not verified.
- The x86 compile depends on qemu-user 11 and .NET 7 under emulation. It works, but slowly.
  A real x86-64 machine or CI runner would be simpler.
- not verified on hardware:
  - latency and energy
  - `ai2d` with 1-channel NCHW
  - the MMZ headroom next to two cameras
  - OV5647 AE interaction with the LED loop
  - imgqual thresholds on fisheye frames
  - `leak_nn.elf` actually running under RT-Smart (it links; the link script is the one from
    `usage_kpu`)
- Disk: `/` was at about 1 GB free when this finished. This directory holds about 290 MB
  (pyenv 168 MB, dotnet 71 MB, data 24 MB, qemu11 14 MB, elf 13 MB). The docker image
  `python:3.11-slim` (amd64) takes 189 MB more; `docker rmi` it when done.
