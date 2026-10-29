# test012: microphone U13 (MSM381ACB026) on the K230 inner codec

Status: **pass** (2026-10-25): 3 x 10 s recordings, a level every second, rms -25.3..-2.8 dBFS; no dropouts, no
repeated lines, NAND and shell stay fast after audio (2026-10-18).

The MEMS microphone U13 on the K230's inner codec (left input), recorded by leakcam_stream.

## Setup

Board on USB-C 1, K230 from NAND. `leakcam_stream -m <s> -o /tmp/mic.raw -x` (levels per 0.5 s, samples as hex over
the console); 16-bit mono, codec left input, PGA 30 dB + ALC 24 dB. Probe sheet: [mic.png](mic.png). Automated:
run.py runs `/sdcard/app/leakcam_stream -m 10` 3 times. Probes: Metrics.

## Needs (build features and dependencies)

K230 audio: codec ADC, MPP audio input (AI), leakcam_stream (Opus).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Boot, meter at msh idle.
2. `leakcam_stream -m 10` 3 times: one level line (rms, peak) per second; the first ~2 s (codec input charging) are
   not judged. Meter while recording.
3. Memory snapshots after each run (leak check).

## Pass

A level reported for every second and rms above the -60 dBFS dead floor in every run; no dropouts, no repeated
64-byte lines, NAND and shell unaffected after a recording.

## Result

- Mic supply at the probes, recording: mic VDD (C96.1) 1.4 V, MIC_BIAS (FB4.2) 1.4 V, mic output DC 0.8 V
  (2026-10-18). MIC_BIAS 1.4 V is below the mic's 1.8 V characterisation point; sensitivity is fine.
- Claps / speech at the port up to -7 dBFS, 35 dB over the floor; quiet room (> 100 Hz) -42 dBFS (mic self-noise at
  54 dB gain); claps clip at 54 dB; no dropouts, no repeated lines, no seams (2026-10-18). Recording:
  [mic_at_port_2026-10-18.wav](mic_at_port_2026-10-18.wav).
- Codec power-up: the first ~2 s clip (input capacitor charging) (2026-10-18).
- NAND unaffected by audio: 5 MB read 0.41 s before, during and after; CPU stalls <= 0.13 ms (2026-10-18).
- 3 x 10 s, a level line every second, rms -25.3..-2.8 dBFS; recording draws 0.232 A vs 0.220 A idle; no leak on
  either chip (2026-10-25, run.py).
- Gain stays 54 dB for now (PGA 20 dB would give 10 dB headroom).

Open:
- Sample rate: the 2026-10-18 setup says 16 kHz, the 2026-10-25 Metrics say 48 kHz mono; which one leakcam_stream
  uses now is not stated.
- Quiet-room level: -42 dBFS (2026-10-18) vs the Metrics' "quiet-room rms -25.3 to -2.8 dBFS" (2026-10-25); the
  2026-10-25 room was likely not quiet, not recorded.

## Metrics (**MISSING** on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - mic VDD C96.1, MIC_BIAS FB4.2 (mic.png).

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | pt_run -> msh 0.45 / 0.45 s (median / max of 1; SPL 0.16, OpenSBI 0.24, RT-Smart 0.25, mounted 0.44, msh 0.45); longest step to mounted 0.19 s (2026-10-25, run.py) | test022 | longest step: |
| This part's boot share | n/a: the codec starts in the program, not in the boot (2026-10-25) | - | |
| Test run time | 219 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | 48 kHz mono, 1 level line per second for 10 s in every run; quiet-room rms -25.3 to -2.8 dBFS, peak 0.0 dBFS (2026-10-25) | SPEEDS.txt | % of the top |
| Current floor / typical / peak | 0.220 / 0.226 / 0.232 A at 5 V (USB-C 1 VBUS meter; K230 at msh idle (agent running): 0.220 A (read in test006-k230-nand-boot-spl); K230 recording the microphone (leakcam_stream -m 10, repeated): 0.232 A) (2026-10-25) | floor (test031) | at the probe above |
| Voltage under load (min) | VBAT 4.35 V min (K230 ADC ch 1, test023 calibration, 9 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | 232 mA x 4.35 V = 1010 mW; per 10 s audio (10.00 s) 10.10 J, 644 uAh; over idle (220 mA) 0.52 J (2026-10-25) | - | scripts/energy.py |
| Per day | 4 x 10 s audio: 2.58 mAh/day, 3.87 % of 2000 mAh per month (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 47.0 / 48.8 / 47.9 C; board 37.7 / 37.8 / 37.7 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 206440 / min 204396 / 206440 B, blocks 16 -> 16, deleted 0; K230 heap available 7025152 / 6867904 / 7312064 B; BL616 no leak (mem_leak.py, 9 snapshots); K230 no leak (mem_leak.py, 9 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | leakcam_stream 1 030 880 B on /sdcard (a product program; loaded only for streams, 0 ms boot) (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | programs at -O3 -flto measured fastest (test008 Flags B: -9 % on the wake path, adopted for the firmware rewrite); the audio path is codec/I2S hardware: the level calculation is negligible (2026-10-25) | the faster one stays | |

## Boot time

n/a: the codec starts in the program (leakcam_stream), not in the boot; its start-up is the first ~2 s of a recording
(input capacitor charging) (2026-10-18, 2026-10-25).

## Battery energy

Recording 0.232 A vs 0.220 A at msh idle (5 V, USB-C 1 VBUS): 10.10 J, 644 uAh per 10 s clip, 0.52 J over idle;
2.58 mAh/day at 4 clips (2026-10-25, Metrics).
