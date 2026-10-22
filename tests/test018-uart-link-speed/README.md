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
(WAKE, READY, Wi-Fi control). Both firmwares change together (product BL616 k230_link, the agent); not done yet.

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test022) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| UART link throughput, round trip | not measured yet | 115200 now; 2 Mbaud top (both chips) | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test031) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | to fill | minimal image (rule) | the image this run used |
| Compile flags | to fill | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | to fill: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

Time from RT-Smart up to READY acknowledged at each baud (link handshake share of the boot).

## Battery energy

Session time saved by a faster baud x the K230's session current = energy saved per wake (the link itself draws next to nothing).

## Obstacles

- **The K230 baud rate did not change, first run** (every rate gave exactly 11 515 B/s, the BL616 had switched) ->
  the serial layer refuses RT_DEVICE_CTRL_CONFIG with RT_EBUSY when the new bufsz differs from the open device's
  (RT_SERIAL_CONFIG_DEFAULT 64 vs UART1's 16384), and the return value was not checked -> copy the device's
  config, change only the baud, check and print the result.
- **The serial RX ring is 64 B** (RT_SERIAL_RB_BUFSZ) -> the bulk window was cut to 48 B.

