# test004: K230 hello from SRAM, 2026-10-17

[k230_hello](../../tools/power-bringup/k230_hello): 0.8 KB bare-metal program the boot ROM loads into SRAM over
USB-C 1 (`make run`), nothing written to the NAND.

| Step | Result | |
|---|---|---|
| `make run` (K230 in USB boot) | `LEAKCAM K230 hello: running from SRAM, hart 0`, then `status n=... mcycle=...` once a second, 800 M cycles apart (core at 800 MHz from the ROM) | pass |
| K230_ALIVE | toggles in `pt_status` | pass |

**Pass.**



## Boot time

n/a: a bring-up step outside the product boot.

## Battery energy

n/a: a one-off bring-up step, no product activity.

## Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

## Obstacles

- **Loader format** -> `--custom-loader` takes a raw binary at 0x80360000, no header.
- **No output** -> the ROM leaves UART/GPIO clocks gated -> enable them (CMU 0x91100024).
- **Slow, garbled lines** -> UART register reads return 0, THRE never seen -> pace characters by time.
- **Flasher "hang"** -> `timeout` around `docker run` leaves the container holding USB -> `timeout` inside the container.
