# test100: Wi-Fi throughput, K230 -> SDIO -> BL616 -> AP

Status: **steps 1-4 pass** (2026-10-25): K230 address 0.33-0.59 s after the card binds; TCP up 27.5 / down 22.1 Mbit/s, UDP up 35.4.

The product path for clips and checks: the K230's lwIP over the NetHub SDIO bridge (test015: bus 21 MB/s, not the
limit) to the BL616's 2.4 GHz radio. Runs on the BL616 test firmware's bridge (`pt_wifi`, the NetHub code of firmware/bl616) with stored credentials
(`wifi_set` on the debug console, or BLE).

## Setup

Board on the bench, USB-C 1 in (USB power, cell charged). BL616: test firmware `bl616_powertest` with
PT_TEST_WIFI and the product's wifi_link.c; credentials from `~/.leakcam_wifi` (never in the repo). K230: image with
the bl616_nethub driver, the test-only `wifi` / `ifconfig` / `ping`, `bl616_rescan` / `bl616_stat` and `iperf`.
Order: `pt_off`, `pt_on`, `pt_wifi bridge`, `pt_run`; the runner sends `bl616_rescan` (the powertest firmware does not
answer the agent's READY). Throughput: home router "VK" (ch 11, RSSI -47 dBm), LAN PC 192.168.1.131 wired with
`pc_tcp_bench.py`, `throughput.py`; address timeline `k230_join_timeline.py`; BL616-only check `pt_poll`. Probes as in
Metrics.

## Needs (build features and dependencies)

- K230 image: bl616_nethub driver, RT-Thread WLAN + lwIP 2.1.2, the `wifi` / `ifconfig` / `ping` shell commands
  (RT_WLAN_MSH_CMD_ENABLE, test-only), `bl616_rescan` / `bl616_stat` (same switch), netutils `iperf`
  (CONFIG_PKG_NETUTILS_IPERF, k230d_leakcam_defconfig, test-only); install.sh patches: SDIO card interrupt armed,
  no DHCP ARP check.
- BL616: test firmware `bl616_powertest` with PT_TEST_WIFI (pt_wifi, pt_poll) and the product's wifi_link.c;
  easyflash (credentials, remembered AP).
- LAN PC: Python 3 for `pc_tcp_bench.py`, an inbound firewall rule for TCP 5001 (local subnet).

## Steps

Built up one function at a time, each passed on the board before the next (user, 2026-10-23):

- **Step 1, the BL616 radio alone** ([pt_wifi.c](../../tests/firmware/bl616_powertest/pt_wifi.c), `PT_TEST_WIFI`):
  `pt_wifi scan` / `join <ssid> [psk]` / `status` / `leave`: the antenna and RF before SDIO; join and DHCP on the
  BL616's own lwIP, each step timed (the product's wifi_check.c path, proven here first).
- Step 2, the NetHub bridge over SDIO: the K230 scans through the BL616.
- Step 3, the K230 joins and gets its address over the bridge; ping to the LAN PC.
- Step 4, TCP throughput K230 <-> PC with RSSI, current, temperature.
- Step 5, the real load (two RTMP streams), after the cameras.

The full list:

1. Join, DHCP, DNS, NTP (time to each step from the WIFI command).
2. iperf (or a temporary TCP/UDP sender on the K230 and a receiver on the LAN PC): TCP up/down, UDP up with loss, at
   1 m and at the install distance; RSSI from the BL616.
3. The real load: two RTMP streams (H.265 2 x ~500 kbit/s + Opus) to MediaMTX: frames dropped, latency.
4. The SDIO bridge's own rate during it (NetHub credits, BL616 busy count).

## Pass

TCP up >= 2 x the two streams' rate with margin at the install distance; no RTMP drops; figures recorded.

## Result

- Step 1, BL616 radio alone (product path, `wifi_link_local_begin()`): radio up 634 ms, scan all channels 2.93 s,
  join to address 4.12 s, internet check 162 ms; weakest AP heard -93 dBm (antenna and RF receive); SDK default
  country code CN (2026-10-23).
- BL616 currents (K230 off, on USB): app awake radio off 62 mA, radio up 64 mA, scanning 85-88 mA, join peak 88 mA,
  joined idle **80 mA** (no Wi-Fi power save) (2026-10-23).
- Steps 2-3, bridge: card bound in 42 ms, scans, joins, DHCP and ping over `wlan0`; loop of 27 scans and 7 joins
  with 0 lost messages, no hung connect, no leak on either chip; `wifi disc` 1.1 s (2026-10-24).
- K230 time to an address (product path, BL616 already joined from its remembered AP in 0.24-0.47 s):
  `bl616_rescan` -> address **0.33-0.59 s** with the SDIO host card interrupt armed and lwIP's DHCP ARP check off
  (install.sh); test path `wifi join` 3.43 s, 2.94 s of it RT-Thread's own scan (the product never calls it)
  (2026-10-25).
- Step 4, TCP throughput on "VK" (ch 11, -47 dBm): **TCP up 27.5 Mbit/s** (peaks 32), TCP down 22.1, **UDP up 35.4**
  (peaks 37), after the K230 driver waits for a slot (TX_WAIT_MS 20, ring 16) and the BL616 NetHub download buffers
  15 -> 26, netdev credits 8 -> 23 (`firmware/bl616/sdk_patch.py`); 9 frames dropped, 3880 credit stalls per 10 s
  upload. The bridge was the limit, not the radio; download stays 22-26 Mbit/s (one frame per SDIO read); runs sag to
  ~16 Mbit/s in their second half on channel 11 (the air). Two H.265 streams (~1 Mbit/s with audio) use a few % of it
  (2026-10-25).
- BL616 RAM free 177.5 -> 155.3 KB with the bigger pool (2026-10-25); the 26 buffers broke the product image's link
  with BLE, fixed by the BL616's lwIP heap 18 -> 6 KB (firmware/bl616/lwipopts_user.h) (2026-10-27, test040).
- BL616-only server check (`pt_poll`, HBN wake -> radio, remembered-AP join, DHCP, DNS + TCP + HTTP 204 -> HBN):
  3 of 3, awake **1709 ms** (radio up 642, association 240, DHCP 747, HTTP 49 ms); ~0.036 mAh per check, ~3.4 mAh/day
  every 15 min; radio up and DHCP are 81 % of the awake time (2026-10-25).

Open:
- Step 5: two RTMP streams to MediaMTX (needs the cameras); UDP loss and the install distance.
- `pt_poll` (the BL616-only server check) not re-run on the 6 KB lwIP heap; the same stack carried DHCP, DNS and an
  HTTP GET in every test106 credential check since (2026-10-30, all passed).
- Test firmware does not take the bridge fully down at `pt_off` (a second run in one BL616 boot gets no address);
  teardown fix open (2026-10-25).
- K230 memory during throughput not read (the runner's `free` parse found nothing).

## Metrics (skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the powertest firmware does not answer the agent's READY; K230 pt_run -> msh 1.95-1.97 s (3 runs, timeline); READY in test022 | target (test022) | longest step: |
| This part's boot share | card bound -> address 0.33-0.59 s (3 runs) after the K230 is up (2026-10-25) | - | |
| Test run time | timeline ~25 s, throughput ~65 s, pt_poll 3 cycles ~25 s; setup: K230 flash 2 min, BL616 flash 1 min each | - | setup (flashing, plugging, waiting) separately |
| Wi-Fi TCP up / down | 27.5 (peaks 32) / 22.1 Mbit/s; UDP up 35.4 (peaks 37) (2026-10-25, raised bridge credits and buffers) | 802.11ax 20 MHz; SDIO 9.4 MB/s end to end | % of the top |
| Current idle / during (min / avg / peak) | K230 + bridge during the join runs: 220 avg, 243 peak mA (user, VBUS, before the fix); BL616 alone joined 80 mA | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | n/a: on USB power (charger rail), not probed this run | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | BL616-only server check ~0.036 mAh (1.71 s awake x step 1 currents) | - | scripts/energy.py |
| Per day | checks every 15 min: ~3.4 mAh/day (ENERGY.txt) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | board 31.2 C (AHT20, pt_status, before the runs); die n/a: chip_temp not read on these runs | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 177544 -> 177252 B, blocks 128 -> 141, deleted 0 (one throughput run, before the pool raise); 155.3 KB free with the raised NetHub pool; K230 n/a this run (the runner's `free` parse found nothing); leak loops of 2026-10-24: no leak on either chip | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | K230 kdimg 45436928 B (iperf in); BL616 test firmware 1114848 B | minimal image (rule) | the image this run used |
| Compile flags | SDK defaults, unchanged by this test | smallest code (rule) | |
| Features over the minimal image | test-only: msh wifi/ifconfig/ping, bl616_rescan/bl616_stat, iperf (none in the product image); size not measured | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: the rate is bound by the link and the radio (CPU idle); revisit in the upload-limit work | the faster one stays | |

## Boot time

Paid in every networked session after the boot: BL616 radio up ~640 ms, remembered-AP association 240 ms, DHCP 747 ms
(BL616 lwIP); the K230 has its address 0.33-0.59 s after `bl616_rescan` (2026-10-25). NTP not measured.

## Battery energy

Join ~4 s at ~88 mA ~ 0.1 mAh (2026-10-23); BL616-only server check ~0.036 mAh (2026-10-25); joined idle 80 mA with no
power save (DTIM sleep is the lever for long sessions). Energy per MB uploaded not measured.
