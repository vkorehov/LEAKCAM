# test003: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-10-25 (run.py, 220 s): pass

8 rail cycles: 3V3 up in 1.69-1.70 ms, off after pt_off every time; USB boot ROM enumerated (29f1:0230).

### 2026-10-17 (first run, by hand)

| Step | Result | |
|---|---|---|
| `pt_status` (rails off) | K230 rails off, reset held | pass |
| `pt_on` | `3V3 up 1.726 ms after K230_PWR` (estimate ~3 ms; soft start) | pass |
| K230_PWR, 0V8, 3V3, 1V8, 1V1, RSTN, 0V8_MIPI, 1V8_MIPI, 1V8_DDR | all within the limits (multimeter) | pass |
| PG_1V8 | not probed (pad too small); implied: 1V1 is up and U7 starts only on PG_1V8 | pass |
| `pt_off` | `3V3 gone 12 ms` (expected 56-82 ms through RD1/RD2) | note |
| USB-C 1 to a Mac with the rails on | VUSB ~2.2 V, the Mac never turns on VBUS: [bug-002](../../bugs/bug-002/README.md) | **fail** |
| `pt_off`, cable in, `pt_on`, `pt_run` | VUSB 5 V; the K230 ROM enumerates as `29f1:0230 Canaan K230 USB Boot Device` | pass |
| Debug serial on USBC2 | CH340 on `/dev/ttyUSB0`; blank NAND: ROM prints `boot failed with exit code 13` at 115200 | pass |

**Pass** (all four K230 rails, boot ROM, debug UART); USB-C attach with the rails on: bug-002, solved by the U2 1-2-3 bridge (2026-10-25).

### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

### Superseded status line

Status: **pass; USB-C attach with rails on: bug-002, solved 2026-10-25** (2026-10-17).

## Obstacles

- **The automated run (2026-10-25) flagged the USB boot as a failed NAND boot** -> the harness expected msh after every boot -> `boot(expect_msh=False)` for USB boots (tools/test-standard/harness.py); the run also waited 1 min on a meter prompt nobody answered -> prompts only with LEAKCAM_METER=ask.
- **Mac on USB-C 1 gives no power with rails on** -> CH340X D+ back-feeds VUSB via D3 (~2.2 V); the USB-C source
  waits for < 0.8 V -> plug USB before `pt_on`; rev 2 fix (bugs/v2.txt).
- **PG_1V8 pad too small** -> implied by 1V1 being up.
- **`pt_off` 12 ms vs 56-82 ms** -> noted, harmless.
