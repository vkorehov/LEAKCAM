# test100: Wi-Fi throughput, K230 -> SDIO -> BL616 -> AP

Status: **planned** (2026-10-21).

The product path for clips and checks: the K230's lwIP over the NetHub SDIO bridge (test014: bus 21 MB/s, not the
limit) to the BL616's 2.4 GHz radio. Needs the product BL616 firmware (open on rev 1) and stored credentials
(`wifi_set` on the debug console, or BLE).

## Steps

1. Join, DHCP, DNS, NTP (time to each step from the WIFI command).
2. iperf (or a temporary TCP/UDP sender on the K230 and a receiver on the LAN PC): TCP up/down, UDP up with loss, at
   1 m and at the install distance; RSSI from the BL616.
3. The real load: two RTMP streams (H.265 2 x ~500 kbit/s + Opus) to MediaMTX: frames dropped, latency.
4. The SDIO bridge's own rate during it (NetHub credits, BL616 busy count).

## Pass

TCP up >= 2 x the two streams' rate with margin at the install distance; no RTMP drops; figures recorded.


## Metrics (to fill on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| Wi-Fi TCP up / down | not measured yet | 802.11ax 20 MHz; SDIO 9.4 MB/s end to end | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |

## Boot time

Time from the WIFI request to associated, DHCP and NTP (paid in every networked session after the boot).

## Battery energy

Battery current: Wi-Fi join (energy per join, paid on every networked session), idle associated, sending; energy per MB uploaded.
