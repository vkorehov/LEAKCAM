# test026: K230 die temperature (TS)

Status: **in progress** (2026-11-01, run.py 1364 s): open board, every load pass: die 37.8 C idle, memtest peak 53.6 C,
KPU 49.4 C, encoder 52.4 C, KPU + encoder 53.6 C (3 min each); the camera load, the closed enclosure and the die
limit open.

`CONFIG_RT_USING_TS=y`: the K230's die temperature sensor. How hot the K230 gets in a session, and with the
enclosure closed.

## Setup

Open board on the bench, test image (of test025, with memtest); K230 die from msh `chip_temp`, board from the BL616's
AHT20 (`pt_status`), every 10 s. Probes: Metrics. Automated: run.py.

## Needs (build features and dependencies)

K230 temperature sensor (TS driver, chip_temp).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Read the TS device at idle right after boot, then every 10 s for 10 min.
2. Same under load: KPU change net in a loop, then the encoder (test027), both cameras when they arrive.
3. Same with the board in the closed enclosure.

## Pass

Readings plausible (idle a few C above room temperature, monotonic warm-up), and under worst-case load well below
the K230's limit (datasheet); note the margin for the enclosure.

## Result

- Open board, CPU + DDR load (memtest): die 44.7 C idle, 52.7 C peak, 48.2 C at the end; rise idle -> memtest 8.0 C
  (2026-10-25, run.py).
- Die ~9.5 C above the board at idle; back to idle within ~20 s after the load: a small die, the heat goes into the
  board (2026-10-21, log [temp_2026-10-21.csv](temp_2026-10-21.csv)).
- `chip_temp` steps are 0.3-0.9 C (12-bit TS); single readings scatter +-0.6 C at idle (2026-10-21).
- Load costs: idle 205 mA (0.85 W), memtest 244 mA avg, 260 peak (1.01 W) on USB-C 1 (2026-10-21).
- Readings plausible, monotonic warm-up: pass for idle and CPU/DDR load (2026-10-21, 2026-10-25).
- Open: KPU change net, encoder (test027) and camera load; the closed enclosure.
- Open: the K230's die limit is not in the repo yet (datasheet to check); the enclosure margin follows from it.
- Open: room temperature not measured (the board sat at 39 C on 2026-10-21).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | 0.893 s (test025, same image) | test022 | |
| This part's boot share | none (TS read on demand) | - | |
| Test run time | 531 s automated (run.py, 2026-10-25) | - | no setup: image of test025 |
| SoC temperature vs time and load | idle 44.7 C, memtest 52.7 C peak (2026-10-25, run.py) | die limit: datasheet, to check | +8.0 C idle -> memtest |
| Current idle / during | 205 mA / 244 mA avg, 260 peak | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT ~4.15 V |
| Voltage under load (min) | VBAT 4.32 V min (K230 ADC ch 1, test023 calibration, 42 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | idle 0.85 W, memtest 1.01 W (+0.16 W) | - | x 4.15 V, by hand |
| Per day | n/a: bench measurement | - | |
| Temperature die / board (start / peak / end) | die 44.7 / 52.7 / 48.2 C (2026-10-25, run.py); board 39.3 / 39.9 / 39.4 C (2026-10-21) | K230 limit (datasheet) | this test |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 206440 / min 204396 / 206440 B, blocks 16 -> 16, deleted 0; K230 heap available 7025152 / 7025152 / 7312064 B; BL616 no leak (mem_leak.py, 42 snapshots); K230 no leak (mem_leak.py, 42 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | kdimg 77.8 MB (/sdcard 176 MiB, /bin 20 MiB layout), kernel ~3.7 MB | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, -ffunction/-fdata-sections, --gc-sections, strip (no LTO) | smallest code (rule) | |
| Features over the minimal image | n/a: chip_temp (SDK TS driver) is part of the floor; memtest 67 120 B on /sdcard for the load (test only) (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (die and board temperature): compiler flags do not change the result (2026-10-25) | the faster one stays | |
| Speed / rate against the top | n/a: temperature test; rise idle -> memtest 8.0 C (2026-10-25) | SPEEDS.txt | % of the top |

## Boot time

n/a (the TS read is in the session); boot temperature recorded at READY.

## Battery energy

The current at each load step logged next to the temperature (self-heating follows power).
