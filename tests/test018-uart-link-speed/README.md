# test018: K230 <-> BL616 UART link: speed, latency, errors

Status: **pass** (2026-10-25): the link runs 4 Mbaud from boot; 8 x 1 MB echoed with 0 errors at 344-350 KB/s (87 % of
the line), 16 B round trip 70-71 us (ISR echo); 6 Mbaud fails (2026-10-22).

The control link (K230 UART1 <-> BL616 UART0, 8N1, test010) carries READY/WAKE/SLEEP/WIFI/TIME frames. Latency
matters (a session waits on each ACK, 300 ms resend timeout); throughput only if it ever carries more.

## Setup

One rate only: the K230 UART driver sets UART1 to LINK_BAUD 4000000 at boot (k230_board/install.sh patch; RT-Smart
serial has no termios); BL616 `pt_uart echo` always runs LINK_BAUD (firmware/bl616/k230_link.h), the echo in the UART
RX interrupt, leaving echo after 300 ms of silence. K230 bench image with `uart_bench 1024`
([leakcam_bench.c](../test017-nand-raw-speed/leakcam_bench.c), SDK copy): 16 B frames timed to their echo, then a
checked bulk transfer with <= 48 B in flight (the serial RX ring is 64 B); it never sets a rate, reports the one set at
boot, buffers its result lines and prints them 1.5 s after the run (the console shares UART1, test039). The runner
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

- Highest clean rate 4 Mbaud: 10 x 1 MB with 0 errors; 6 Mbaud bulk fails (6 600 bad bytes, timeout); 1.5x margin
  (2026-10-22 sweep). 115200 gave 1 479 us round trip and 11 515 B/s: 4 Mbaud cuts each link message 20-27x.
- Adopted: LINK_BAUD 4000000 in bl616/k230_link.h, UART1 set in the K230 UART driver at boot; single rate, nothing
  switches it (2026-10-22).
- On the single-rate images: 8 x 1 MB, 0 bad bytes, 344360-349858 B/s (87 % of the line); 16 B round trip median
  70-71 us, max 74 us with the BL616 echo in the UART RX interrupt (polled echo: 53-56 us, 2026-10-22) (2026-10-25,
  run.py).
- No leak on either chip over 8 runs (2026-10-25).

Open:
- The product images (firmware rewrite) still to be rerun on this test.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | pt_run -> msh 0.47 / 0.47 s (median / max of 1; SPL 0.16, OpenSBI 0.24, RT-Smart 0.25, mounted 0.46, msh 0.47); longest step to mounted 0.21 s (2026-10-25, run.py) | target (test022) | longest step: |
| This part's boot share | n/a: the link is up from the UART driver's init (0.27 s), no step of its own (2026-10-25) | - | |
| Test run time | 97 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| UART link throughput, round trip | 349858 B/s = 87 % of the 4 Mbaud line; 16 B round trip median 70-71 us, max 74 us; 0 bad bytes / frames over 8 x 1 MB (2026-10-25) | 4 Mbaud line (LINK_BAUD; 6 Mbaud fails) | % of the top |
| Current idle / during (min / avg / peak) | msh idle 0.220 A (test006, 2026-10-25); during the bench not read (the link's share is below the meter's step, test010) (2026-10-25, from recorded readings) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | VBAT 4.35 V min (K230 ADC ch 1, test023 calibration, 9 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a: the link's own current is below the meter's step next to the K230 (test010) (2026-10-25) | - | scripts/energy.py |
| Per day | n/a: a few link frames per session, negligible (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 35.2 / 43.1 / 41.9 C; board 29.6 / 32.5 / 32.5 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 206440 / min 204396 / 206440 B, blocks 16 -> 16, deleted 0; K230 heap available 7313600 B steady; BL616 no leak (mem_leak.py, 9 snapshots); K230 no leak (mem_leak.py, 9 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | K230 kdimg 45441024 B, BL616 test firmware 1114848 B (2026-10-25, on the board at the run) | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -O2 (SDK default), no LTO (2026-10-25) | smallest code (rule) | |
| Features over the minimal image | kernel test commands in leakcam_bench.o: 11.2 KB code + 0.6 KB RAM for all 13 (ddr/nand/uart_bench, boot_times, plt_*, rtc_*, pad_read, bat_watch); no init, 0 ms boot; not in the product image (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (the UART line rate (4 Mbaud)): compiler flags do not change the result (2026-10-25) | the faster one stays | |

## Boot time

n/a: the link is up from the UART driver's init (0.27 s), no step of its own; boot to app init unchanged by 4 Mbaud
(0.483 s, 2026-10-22).

## Battery energy

n/a: the link's own current is below the meter's step next to the K230 (test010); a faster rate saves session time
only (2026-10-25).
