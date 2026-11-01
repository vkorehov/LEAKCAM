# test026: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-01 (run.py, 1364 s): pass

die idle 37.8 C, memtest peak 53.6 C, end 50.0 C.

- KPU: die 49.1 -> 49.4 C peak over 3 min
- encoder: die 49.4 -> 52.4 C peak over 3 min
- KPU + encoder: die 52.1 -> 53.6 C peak over 3 min


### Automated run 2026-10-25 (run.py, 531 s): pass

die idle 44.7 C, memtest peak 52.7 C, end 48.2 C.

### Results (2026-10-21, open board on the bench, test image, K230 booted at 20:23:43)

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

Old status line (2026-10-21): **in progress** (2026-10-21): open board, idle and memtest load measured; KPU/encoder/camera load and the closed enclosure open.

### Superseded metric values

- Test run time (2026-10-21): "6 min (20:27:04-20:33:06)", note "no setup: image of test025".
- SoC temperature vs time and load (2026-10-21): "idle 48.5 C, memtest 53.3 C peak after 2 min", note "+4.8 C at +39 mA".
- Temperature die (start / peak / end) (2026-10-21): "49.1 / 53.3 / 49.1 C".

## Obstacles

- **Three die readings of 5.5 C in the 8 h DDR soak** (2026-10-24, tests/test025 runner) -> the runner's regex took the
  first number after `chip_temp`, and a memtest pass line ("in 5.5 s") was interleaved into its answer -> no data,
  dropped from the summary; read the "chip temperature: x C" line explicitly in later runners.
