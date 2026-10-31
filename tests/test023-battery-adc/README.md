# test023: Battery ADC (K230 ADC_1, VBAT/3)

Status: **pass** (2026-10-25): calibrated 64-read mean within +-10 mV of the meter at 4.07-4.37 V; low cell and load open.

K230 ADC_1 (ball A6) reads VBAT through R59 200 k / R61 100 k, switched by Q1 from the K230's 3V3 (readable only
while the K230 runs). The agent converts raw x 1800 mV x 3 / 4095 (12-bit, 1.8 V reference). The bug-004 rev 1
workaround cuts off on this reading (3.10 V under load), so it must agree with a meter.

## Setup

K230 running (BL616 test firmware: `pt_on`, `pt_run`), K230 console (USBC2), meter on C11 pad 1 (BAT) and C15 pad 1
(VBAT, what the ADC sees), GND on the caps' other pads.

![probe points](adc_points.png)

[adc_points.png](adc_points.png): VBAT on C15 pad 1, BAT on C11 pad 1, GND on each cap's other pad (top side, U1 corner).

## Needs (build features and dependencies)

K230 ADC driver (adc probe/read), msh adc command.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. K230 shell: `adc probe adc`, then `adc read 1` five times; VBAT = raw x 5400 / 4095 mV.
2. USB in (charging): ADC vs meter VBAT and BAT.
3. USB out (battery): ADC vs meter; repeat at a lower cell voltage later (after a discharge), and with the KPU or
   LEDs loading the cell (the cutoff reads under load).

## Pass

ADC within +-30 mV of the meter's VBAT (after a one-point correction if it is a constant offset), noise <= 10 mV
between reads.

## Result

- On USB, C15 (the ADC input) is the BQ24072 output rail (~4.4 V), not the cell; on battery the BAT-FET joins OUT to
  the cell and the ADC reads the cell (the only case the bug-004 cutoff needs). The firmware must not report the USB
  reading as battery level (2026-10-21).
- Raw single reads: offset +47..+65 mV against the meter, noise sd 52 mV (target 10); a mean of 64 reads brings the
  noise to ~6-7 mV (2026-10-21).
- Calibration from 7 pairs at 4.07-4.37 V (USB and battery): meter = ADC x 0.9868 (full scale 5329 mV instead of
  5400), mean of 64 conversions; within +-10 mV of the meter: **pass** at a full cell. In
  attic/firmware/k230_agent/leakcam_agent.c (BAT_FULL_SCALE_MV) (2026-10-21).
- VBAT on USB 4355-4395 mV over 8 samples (idle, then memtest), spread 40 mV, with the calibration (2026-10-25,
  run.py).
- A short on the cell trips the pack's PCM, which stays latched (BAT ~1.5 V) until USB is plugged in: a shorted unit
  stays dead until USB (2026-10-21, battery case; cause by the user).

Open:
- Low-cell point (~3.4 V): tells gain from offset; re-fit the calibration then.
- Reading under load on the cell: n/a, no battery measurements (user, 2026-10-30); the cut-off under load is test038's.
- CH340 (U2) drops off USB when USB-C 1 is plugged in while on battery (reproduced 2026-10-21 19:51); candidates
  D3 VCC back-fed from U2's D+ or a 3V3 dip resetting U2, both gone in rev 2 (bugs/v2.txt 3/5); the cause not measured.

## Metrics (2026-10-21)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1 (ADC input), BAT C11.1 (adc_points.png).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init | 0.88 s | test022 | |
| This part's boot share | none (ADC probed on demand) | - | |
| Test run time | 66 s automated (run.py, 2026-10-25), no setup | - | |
| ADC accuracy / noise | +60 mV / sd 52 mV single; calibrated 64-read mean within +-10 mV, sd ~6 mV | +-30 mV / 10 mV | pass averaged |
| Current on battery, K230 at msh | n/a: no battery measurements (user, 2026-10-30); 202-203 mA on USB (above) | - | VBAT 4.07-4.12 V |
| Voltage under load (min) | VBAT 4.36 V min (K230 ADC ch 1, test023 calibration, 8 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Current during reads | 202-203 mA (= msh idle) | floor 202 | reads cost nothing measurable |
| Power, energy per reading | below the meter's resolution | - | |
| Per day | n/a | - | |
| Temperature die / board (start / peak / end) | die 34.9 / 43.7 / 43.7 C; board 29.2 / 31.8 / 31.8 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 207136 / min 204396 / 207136 B, blocks 19 -> 19, deleted 0; K230 heap available 7025152 / 6822976 / 7111808 B; BL616 no leak (mem_leak.py, 8 snapshots); K230 no leak (mem_leak.py, 8 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | kdimg 77.8 MB (/sdcard 176 MiB, /bin 20 MiB layout), kernel ~3.7 MB | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, -ffunction/-fdata-sections, --gc-sections, strip (no LTO) | smallest code (rule) | |
| Features over the minimal image | n/a: the ADC driver is part of the product floor (battery cut-off, test038) (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (ADC conversion): compiler flags do not change the result (2026-10-25) | the faster one stays | |

## Boot time

n/a: one conversion during the session (not at boot).

## Battery energy

n/a for the reading itself (Q1 switches the divider only while the K230 runs: ~14 uA then, nothing while off); the test gives the voltage the energy audit (test104) logs.
