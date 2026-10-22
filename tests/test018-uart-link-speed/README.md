# test018: K230 <-> BL616 UART link: speed, latency, errors

Status: **planned** (2026-10-21).

The control link (K230 UART1 <-> BL616 UART0, 115200 8N1, test010) carries READY/WAKE/SLEEP/WIFI/TIME frames.
Latency matters (a session waits on each ACK, 300 ms resend timeout); throughput only if it ever carries more.

## Setup

To fill: hookup, firmware / image, and the probes named in Metrics.

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

Not run yet.

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

None yet.
