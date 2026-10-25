# test018: K230 <-> BL616 UART link: speed, latency, errors

Status: **pass** (2026-10-22): clean up to 4 Mbaud (10 MB, 0 errors), 6 Mbaud fails; latency 1.48 ms -> 55 us.

The control link (K230 UART1 <-> BL616 UART0, 115200 8N1, test010) carries READY/WAKE/SLEEP/WIFI/TIME frames.
Latency matters (a session waits on each ACK, 300 ms resend timeout); throughput only if it ever carries more.

## Setup

K230 bench image with `uart_bench <baud> [kb]` ([leakcam_bench.c](../test017-nand-raw-speed/leakcam_bench.c), SDK copy):
UART1 at the rate, 200 frames of 16 B timed to their echo, then a checked bulk transfer with <= 48 B in flight (the
serial RX ring is 64 B). BL616 bl616_powertest `pt_uart echo <baud>`: a polling task returns every byte. Both ends
switched per rate; leakcam_agent had ended by itself (it owns UART1 otherwise). USB-C 1 VBUS meter.

## Needs (build features and dependencies)

K230 UART1 at high baud; BL616 UART0.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. BL616 test firmware: an echo mode on UART0 (`pt_uart echo`); K230 temporary command sends frames and times the
   echo: round-trip latency (median, max) at 115200.
2. Bulk both ways: 64 KB with a checksum, errors counted, at 115200, 230400, 460800, 921600, 2 M baud (both ends
   switched together).
3. The highest baud with 0 errors over 10 MB, and the latency gained.

## Pass

Latency and error-free limit recorded; the product keeps 115200 unless a faster rate is clean with margin and buys
session time.

## Results

### 2026-10-22

| Baud | Latency 16 B round trip (median / max) | Throughput (64 KB checked) | of the line | Errors |
|---|---|---|---|---|
| 115 200 | 1 479 / 1 488 us | 11 515 B/s | 99.96 % | 0 |
| 230 400 | 744 / 748 us | 22 989 B/s | 99.8 % | 0 |
| 460 800 | 376 / 384 us | 45 926 B/s | 99.7 % | 0 |
| 921 600 | 208 / 209 us | 83 400 B/s | 90.5 % | 0 |
| 1 500 000 | 131 / 139 us | 135 681 B/s | 90.5 % | 0 |
| 2 000 000 | 100 / 101 us | 180 784 B/s | 90.4 % | 0 |
| 3 000 000 | 68 / 75 us | 271 338 B/s | 90.4 % | 0; **10 x 1 MB: 0 errors**, 271 KB/s |
| **4 000 000** | **55 / 70 us** | **350 979 B/s** | 87.7 % | 0; **10 x 1 MB: 0 errors**, 354 KB/s |
| 6 000 000 | 41 / 52 us (frames ok) | bulk fails: 6 600 bad bytes, timeout | - | **fail** |

Above 460800 the ~10 % gap is the bench's window protocol, not the line. **Highest clean rate 4 Mbaud** (1.5x below
the failure at 6 Mbaud); the link runs 115200 today: adopting 3-4 Mbaud cuts every K230 <-> BL616 message 20-27x
(WAKE, READY, Wi-Fi control). **Adopted 2026-10-22 (user: "move to top stable speeds")**: LINK_BAUD 4000000 in
bl616/k230_link.h, UART1 set in the K230 UART driver by k230_board/install.sh (RT-Smart serial has no termios, the
agent cannot set it). Code only; the product images are rebuilt with the firmware rewrite, then this test reruns on them.

### 2026-10-22, on the single-rate images (4 Mbaud from boot, nothing switches it)

K230 image with the UART driver patch, BL616 test firmware 06:59:02 (`pt_uart echo` = LINK_BAUD): `uart_bench 1024`
reports "uart1 at 4000000 baud" without setting it. **10 x 1 MB: 0 bad bytes, 353.0-353.3 KB/s** (88 % of the
line); 16 B round trip median 53-56 us, max 66 us, 2000 of 2000 ok. Die 48.2 C before and after. Boot to app init
0.483 s (unchanged). Current during the soak not measured (asked, no reading).

### Automated run 2026-10-25 (run.py, 97 s): pass

8 of 8 runs of uart_bench 1024 at 4 Mbaud: 0 errors, 344360-349858 B/s.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | pt_run -> msh 0.47 / 0.47 s (median / max of 1; SPL 0.16, OpenSBI 0.24, RT-Smart 0.25, mounted 0.46, msh 0.47); longest step to mounted 0.21 s (2026-10-25, run.py) | target (test022) | longest step: |
| This part's boot share | n/a: the link is up from the UART driver's init (0.27 s), no step of its own (2026-10-25) | - | |
| Test run time | 97 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| UART link throughput, round trip | 349858 B/s = 87 % of the 4 Mbaud line; 16 B round trip median 70-71 us, max 74 us; 0 bad bytes / frames over 8 x 1 MB (2026-10-25) | 115200 now; 2 Mbaud top (both chips) | % of the top |
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

Time from RT-Smart up to READY acknowledged at each baud (link handshake share of the boot).

## Battery energy

Session time saved by a faster baud x the K230's session current = energy saved per wake (the link itself draws next to nothing).

## Obstacles

(Since 2026-10-23 the K230 console is the BL616's `/dev/ttyACM1`, over the link UART from the first SPL line,
up across cold boots (tests/test039): the CH340 problems below are history, kept as the record of these runs.)

- **The K230 baud rate did not change, first run** (every rate gave exactly 11 515 B/s, the BL616 had switched) ->
  the serial layer refuses RT_DEVICE_CTRL_CONFIG with RT_EBUSY when the new bufsz differs from the open device's
  (RT_SERIAL_CONFIG_DEFAULT 64 vs UART1's 16384), and the return value was not checked -> copy the device's
  config, change only the baud, check and print the result.
- **The serial RX ring is 64 B** (RT_SERIAL_RB_BUFSZ) -> the bulk window was cut to 48 B.
- **The rate fell back to 115200 after the first run on the 4 Mbaud image** (2026-10-22 09:51, my own bench bug):
  uart_bench restored 115200 on exit ("back to the link's rate for the agent", from before the change); 9 queued runs
  then timed out at 15 + 20 s each (~6 min) -> **one rate only** (user: "keep single speed", "don't like switching
  speeds"): uart_bench takes no rate and never sets one, it reports what the UART driver set at boot; BL616 `pt_uart`
  always runs LINK_BAUD (firmware/bl616/k230_link.h, included by the test firmware too). The sweep above stays as
  the record of how 4 Mbaud was chosen.
- **Soak read nothing for 2 min** (10:01-10:04, my mistake): the CH340 re-enumerated after the NAND boot as
  root:dialout and my script skipped setfacl -> permanent fix: udev rule tools/power-bringup/99-leakcam.rules (all four
  board USB devices 0666 on every plug; user: "remember already permissions").
- **Queued msh commands** (my mistake): commands sent every 3.6 s while each run took 35 s queued up in msh ->
  send the next only after the previous result line.

None in the run of 2026-10-25.
- **uart_bench results never reached the console** (2026-10-25, automated run): since test039 the K230 console shares
  UART1 with the link, so while the BL616 echoes for the bench the result lines were echoed back into the K230 ->
  uart_bench buffers its lines and prints them 1.5 s after the run; the BL616 leaves the echo after 300 ms of silence
  (tests/test017-nand-raw-speed/leakcam_bench.c, bl616_powertest main.c).
- **The command could not be typed** -> the bridge stops while echoing -> `pt_uart echo <command>` sends it over the
  link itself; the K230 shell's echo of that line came back and looped as input -> dropped for the first 5 ms.
- **The idle-priority echo task managed ~5 KB/s** (the Wi-Fi tasks of the test firmware take the CPU; the 32-byte FIFO
  overflows in 80 us at 4 Mbaud) -> the echo moved into the UART RX interrupt: 347 KB/s, round trip 70 us (55 us
  polled on 2026-10-22, the ISR's cost).
- **leakcam_agent's READY frames corrupted the bench stream** (40 of 200 frames bad, bulk stalled) -> the runner stops
  the agent first (on 2026-10-22 it had ended by itself).
