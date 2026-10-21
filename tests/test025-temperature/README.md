# test025: K230 die temperature (TS)

Status: **in progress** (2026-10-21): open board, idle and memtest load measured; KPU/encoder/camera load and the closed enclosure open.

`CONFIG_RT_USING_TS=y`: the K230's die temperature sensor. How hot the K230 gets in a session, and with the
enclosure closed.

## Steps

1. Read the TS device at idle right after boot, then every 10 s for 10 min.
2. Same under load: KPU change net in a loop, then the encoder (test026), both cameras when they arrive.
3. Same with the board in the closed enclosure.

## Pass

Readings plausible (idle a few C above room temperature, monotonic warm-up), and under worst-case load well below
the K230's limit (datasheet); note the margin for the enclosure.


## Result (2026-10-21, open board on the bench, test image, K230 booted at 20:23:43)

Every 10 s: K230 die from msh `chip_temp`, board from the BL616's AHT20 (`pt_status`). Log:
[temp_2026-10-21.csv](temp_2026-10-21.csv).

| Phase | Time | Die (mean / min / max) | Board AHT20 | Meter (USB-C 1) |
|---|---|---|---|---|
| idle (msh, agent done) | 20:27:04-20:29:54 | 48.5 / 47.9 / 49.1 C | 39.0-39.2 C | 205 mA |
| memtest 20 passes (`&`, CPU + DDR) | 20:30:06-20:32:06 | 52.0 / 49.4 / 53.3 C | 39.2-39.9 C | 244 mA avg, 260 peak |
| cooling | 20:32:16-20:33:06 | 49.7 / 48.8 / 52.4 C | 39.9 -> 39.4 C | - |

- Die ~9.5 C above the board at idle; memtest adds ~+4.5 C in 2 min, not settled yet (still rising at the end);
  back to idle within ~20 s after the load: a small die, the heat goes into the board.
- The board itself sits at 39 C: the K230 has been on most of the time since 19:31 (off 19:57-20:04, short power cycles); room temperature not measured.
- `chip_temp` steps are 0.3-0.9 C (12-bit TS); single readings scatter +-0.6 C at idle.
- Plausible readings, monotonic warm-up: **pass** for idle and CPU/DDR load. The K230's die limit is not in the repo
  yet (datasheet to check); the margin for the enclosure follows from it.

## Metrics (to fill on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | 0.893 s (test024, same image) | test021 | |
| This part's boot share | none (TS read on demand) | - | |
| Test run time | 6 min (20:27:04-20:33:06) | - | no setup: image of test024 |
| SoC temperature vs time and load | idle 48.5 C, memtest 53.3 C peak after 2 min | die limit: datasheet, to check | +4.8 C at +39 mA |
| Current idle / during | 205 mA / 244 mA avg, 260 peak | floor (test030) | USB-C 1 VBUS meter, D1 off; VBAT ~4.15 V |
| Power, energy per operation | idle 0.85 W, memtest 1.01 W (+0.16 W) | - | x 4.15 V, by hand |
| Per day | n/a: bench measurement | - | |
| Temperature die / board (start / peak / end) | 49.1 / 53.3 / 49.1 C, board 39.3 / 39.9 / 39.4 C | K230 limit (datasheet) | this test |

## Boot time

n/a (the TS read is in the session); boot temperature recorded at READY.

## Battery energy

The current at each load step logged next to the temperature (self-heating follows power).
