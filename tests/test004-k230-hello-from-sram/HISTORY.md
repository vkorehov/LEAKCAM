# test004: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-10-25 (run.py, 170 s): pass

3 USB boots: k230_hello loaded in 4.2, 4.2, 4.2 s, K230_ALIVE toggling (avg half-period 1000, 1000, 1000 ms).

### First run 2026-10-17: pass

| Step | Result | |
|---|---|---|
| `make run` (K230 in USB boot) | `LEAKCAM K230 hello: running from SRAM, hart 0`, then `status n=... mcycle=...` once a second, 800 M cycles apart (core at 800 MHz from the ROM) | pass |
| K230_ALIVE | toggles in `pt_status` | pass |

**Pass.**

#### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

### Former placeholders (2026-10-17 README)

- Setup: **MISSING**; the setup is in the introduction above and in the Results.
- Steps: **MISSING**; the steps are the rows of the Results tables.
- Pass: **MISSING**; every Results row carries its pass / fail.

## Obstacles

- **Loader format** -> `--custom-loader` takes a raw binary at 0x80360000, no header.
- **No output** -> the ROM leaves UART/GPIO clocks gated -> enable them (CMU 0x91100024).
- **Slow, garbled lines** -> UART register reads return 0, THRE never seen -> pace characters by time.
- **Flasher "hang"** -> `timeout` around `docker run` leaves the container holding USB -> `timeout` inside the container.
