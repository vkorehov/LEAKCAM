# test018: K230 <-> BL616 UART link: speed, latency, errors

Status: **pass** (2026-10-31): the link runs 2 Mbaud from boot (stability, test039); 8 x 1 MB echoed with 0 errors
at 180740 B/s (90 % of the line), 16 B round trip median 131 us, max 137 us; 10 x 1 MB clean with the Wi-Fi bridge
bound too. 4 Mbaud was clean here but lost short writes in test039; 6 Mbaud fails.

The control link (K230 UART1 <-> BL616 UART0, 8N1, test010) carries READY/WAKE/SLEEP/WIFI/TIME frames. Latency
matters (a session waits on each ACK, 300 ms resend timeout); throughput only if it ever carries more.

## Setup

One rate only: the K230 UART driver sets UART1 to LINK_BAUD 2000000 at boot (k230_board/install.sh patch; RT-Smart
serial has no termios); BL616 `pt_uart echo` always runs LINK_BAUD (firmware/bl616/k230_link.h), the echo in the UART
RX interrupt, leaving echo after 300 ms of silence. K230 bench image with `uart_bench 1024`
([leakcam_bench.c](../test017-nand-raw-speed/leakcam_bench.c), SDK copy): 16 B frames timed to their echo, then a
checked bulk transfer with <= 48 B in flight (the serial RX ring is 64 B); it never sets a rate, reports the one set at
boot, buffers its result lines and prints them 1.5 s after the run (the console shares UART1, test039). While it runs,
link_mux holds every console line (written after) and drops link frames: other UART1 output in the echo stream
looked like lost bytes (2026-10-31, HISTORY). The runner
stops leakcam_agent first (it owns UART1). USB-C 1 VBUS meter. Probes: Metrics.

## Needs (build features and dependencies)

K230 UART1 at high baud; BL616 UART0.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Stop leakcam_agent; BL616 `pt_uart echo` (sent over the link itself).
2. `uart_bench 1024` 8 times: round-trip latency (median, max) of 16 B frames and 1 MB checked bulk, bad bytes
   counted.
3. Memory snapshots per run (leak check).

## Pass

Latency and error-free limit recorded; 0 bad bytes / frames at the link rate; the product keeps a rate only when it is
clean with margin.

## Result

- 2 Mbaud (2026-10-31, run.py): 8 x 1 MB, 0 bad bytes, 180740 B/s (90 % of the 200000 B/s line); 16 B round trip
  median 131 us, max 137 us; 200 of 200 frames per run. With the Wi-Fi bridge bound on the K230: 10 x 1 MB, 0 lost,
  BL616 RX FIFO at most 24 of 32 at ISR entry, 0 overflows; K230 uart1 0 framing / parity / overrun.
- Rate history: 6 Mbaud bulk fails (6 600 bad bytes); 4 Mbaud clean in bulk (8 x 1 MB, 344-350 KB/s, 70-71 us) but
  lost ~0.5 % of short console writes after an idle gap (test039): 2 Mbaud adopted (2026-10-30). 115200 gave 1 479 us
  round trip and 11 515 B/s (SPEEDS.txt).
- No leak on either chip over 8 runs (2026-10-31).

Open:
- 2.5 / 2.857 / 3.333 Mbaud (exact on both UART clocks) never tested built in at boot: only a run-time switch that
  missed the K230's fractional divisor was tried.
- The product images (firmware rewrite) still to be rerun on this test.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | pt_run -> msh 0.52 s (1 boot; SPL 0.16, OpenSBI 0.27, RT-Smart 0.28, mounted 0.51, msh 0.52); longest step to mounted 0.23 s (2026-10-31, run.py) | target (test022) | longest step: |
| This part's boot share | n/a: the link is up from the UART driver's init (0.27 s), no step of its own (2026-10-25) | - | |
| Test run time | 118 s automated (run.py, 2026-10-31), no setup | - | setup (flashing, plugging, waiting) separately |
| UART link throughput, round trip | 180740 B/s = 90 % of the 2 Mbaud line; 16 B round trip median 131 us, max 137 us; 0 bad bytes / frames over 8 x 1 MB (2026-10-31) | 2 Mbaud line (LINK_BAUD; 4 lost short writes, 6 fails) | % of the top |
| Current idle / during (min / avg / peak) | msh idle 0.220 A (test006, 2026-10-25); during the bench not read (the link's share is below the meter's step, test010) (2026-10-25, from recorded readings) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | VBAT 4.35 V min (K230 ADC ch 1, test023 calibration, 9 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a: the link's own current is below the meter's step next to the K230 (test010) (2026-10-25) | - | scripts/energy.py |
| Per day | n/a: a few link frames per session, negligible (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 40.1 / 45.8 / 45.8 C; board 32.0 / 34.4 / 34.4 C (AHT20) (2026-10-31) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 176464 / 174296 / 174296 B (bench task once), blocks 14 -> 16, deleted 0; K230 heap available 7313600 B steady; no leak on either (mem_leak.py, 9 snapshots) (2026-10-31) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | K230 kdimg 45441024 B, BL616 test firmware 1313712 B (2026-10-31, on the board at the run) | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -O2 (SDK default), no LTO (2026-10-25) | smallest code (rule) | |
| Features over the minimal image | kernel test commands in leakcam_bench.o: 11.2 KB code + 0.6 KB RAM for all 13 (ddr/nand/uart_bench, boot_times, plt_*, rtc_*, pad_read, bat_watch); no init, 0 ms boot; not in the product image (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (the UART line rate, 2 Mbaud): compiler flags do not change the result (2026-10-31) | the faster one stays | |

## Boot time

n/a: the link is up from the UART driver's init (0.27 s), no step of its own; pt_run -> msh 0.52 s at 2 Mbaud
(2026-10-31).

## Battery energy

n/a: the link's own current is below the meter's step next to the K230 (test010); a faster rate saves session time
only (2026-10-25).
