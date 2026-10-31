# test013: KPU, change net (leakcam_change.kmodel)

Status: **pass** (2026-10-25): int8 net whole on the KPU, 0.5 ms per inference, one distance 2.9 ms median, output
identical on every run; threshold 0.43 from the 219 real pairs (2026-10-19).

The change net ([attic/firmware/k230_nn](../../attic/firmware/k230_nn/README.md)) on the KPU against the 15 check pairs of
`make_embed.py` (`data/embed_pairs.npy` with TensorFlow's distances). Temporary test programs, in the SDK copy only
(not in the repo): `leakcam_kputest` (the pairs through `change_distance()`), `leakcam_kpurun` (any kmodel, fixed
input), `leakcam_kmin` (a one-layer model with known output), Canaan's `yolov8_image.elf`.

## Setup

The test image with `leakcam_kputest` on /sdcard (SDK copy only) and the factory int8 `leakcam_change.kmodel`
(`compile_embed.py`); `CONFIG_RT_USING_REGULATOR` in `k230d_leakcam_defconfig`. Automated: run.py runs
`leakcam_kputest 50` 8 times on synthetic frames. Probes: see Metrics.

## Needs (build features and dependencies)

KPU driver (gnne), nncase runtime, AI2D, the change-net kmodel; leakcam_wake.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Load the kmodel (through a stream) and run the check pairs through `change_distance()`: distances against
   TensorFlow, time per inference and per pair.
2. All 219 `make_embed.py` pairs on the KPU: share of each pair kind above the threshold, threshold choice.
3. Automated (run.py): 8 x `leakcam_kputest 50` on synthetic frames: time per distance, identical output.

## Pass

Distances match TensorFlow closely (same / change separated as TF does), the whole net on the KPU, the output
identical on every run.

## Result

- int8 kmodel (211 KB) is the factory `leakcam_change.kmodel`: 0.5 ms per inference, 23 ms per pair, 4.4 ms per check
  over the 219 pairs; vs TensorFlow mean 0.031, max 0.15 per pair, correlation 0.955 (2026-10-19, user chose int8).
- Threshold **0.43** (`CHANGE_THRESHOLD`) from the int8 distances on the 219 real pairs: light 0 % and realdry 0 %
  above it; wet 51 %, wet + light 40 %, small 25 % / small + light 12 %, wipe 53 % (TF at 0.44: 50 / 42 / 25 / 12 /
  43 %) (2026-10-19).
- One distance (2 inferences + cosine) 2.9 / 2.9 / 3.1 ms (min / median / max, 8 x 50), identical output every run,
  no leak (run.py, 2026-10-25; synthetic frames: timing, not detection quality).
- What it takes (2026-10-18/19): `CONFIG_RT_USING_REGULATOR` (else the KPU is never powered and the first inference
  hangs); the kmodel loaded through a stream (a memory span gives zeros / NaN); int8, not int16 (int16 leaves 40
  convolutions in float32 on the CPU: 6.3 s per inference); `change_distance()` returns -1 (change reported) on a
  non-finite distance. The product image with the int8 net boots and the agent ends its session normally.

Open:
- Energy per change check not measured (Power / Per day **MISSING**).

## Metrics (**MISSING** on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | pt_run -> msh 0.46 / 0.46 s (median / max of 1; SPL 0.16, OpenSBI 0.24, RT-Smart 0.25, mounted 0.45, msh 0.46); longest step to mounted 0.20 s (2026-10-25, run.py) | test022 | longest step: |
| This part's boot share | n/a: the model loads in the program (kputest prints the load time) (2026-10-25) | - | |
| Test run time | 54 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | one distance (2 inferences + cosine) 2.9 / 2.9 / 3.1 ms (min / median / max, 8 x 50) (2026-10-25) | SPEEDS.txt | % of the top |
| Current floor / typical / peak | 0.220 / 0.220 / 0.220 A at 5 V (USB-C 1 VBUS meter; K230 at msh idle (agent running): 0.220 A (read in test006-k230-nand-boot-spl)) (2026-10-25) | floor (test031) | at the probe above |
| Voltage under load (min) | VBAT 4.35 V min (K230 ADC ch 1, test023 calibration, 9 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | the KPU loop reads as idle (0.220 A, the meter's 1 mA step): one change check (2.9 ms) costs < 1 mA x 5 V x 2.9 ms = 15 uJ over idle (2026-10-30, from the recorded readings) | - | scripts/energy.py |
| Per day | < 15 uJ x 2 cameras per wake: negligible next to the session (test102) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 39.5 / 42.5 / 42.5 C; board 31.8 / 32.8 / 32.8 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 208608 / min 204396 / 208608 B, blocks 14 -> 14, deleted 0; K230 heap available 7025152 / 7025152 / 7312064 B; BL616 no leak (mem_leak.py, 9 snapshots); K230 no leak (mem_leak.py, 9 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | leakcam_kputest 4 397 680 B on /sdcard (test only; mostly the nncase runtime the product's leakcam_wake carries anyway); 0 ms boot (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (the KPU runs the net; the CPU part is the cosine): compiler flags do not change the result (2026-10-25) | the faster one stays | |

## Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: KPU power-up and kmodel load time (at boot or at first use in the session).

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: extra current during inference vs idle; energy per change check (4.4 ms per check).
