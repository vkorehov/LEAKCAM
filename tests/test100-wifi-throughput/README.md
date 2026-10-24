# test100: Wi-Fi throughput, K230 -> SDIO -> BL616 -> AP

Status: **step 1 in progress** (2026-10-23): `pt_wifi` scan and join pass: (radio up 635 ms, 11 APs in 2.9 s, own AP -29 dBm); step 1 pass (BL616 alone: join 3.8 s, internet 70-158 ms, joined 80 mA); steps 2+3 pass (K230 over the NetHub bridge: scan 4 s, join, DHCP, ping 8.8.8.8); open: the join's 10 s scan wait, step 4 throughput.

The product path for clips and checks: the K230's lwIP over the NetHub SDIO bridge (test015: bus 21 MB/s, not the
limit) to the BL616's 2.4 GHz radio. Needs the product BL616 firmware (open on rev 1) and stored credentials
(`wifi_set` on the debug console, or BLE).

## Setup

To fill: hookup, firmware / image, and the probes named in Metrics.

## Needs (build features and dependencies)

To fill: the kernel options / drivers, SDK components, libraries and programs this test uses (the union over all
tests is what the minimal image keeps).

## Steps

Built up one function at a time, each passed on the board before the next (user, 2026-10-23):

- **Step 1, the BL616 radio alone** ([pt_wifi.c](../../tools/power-bringup/bl616_powertest/pt_wifi.c), `PT_TEST_WIFI`):
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

## Results

### Step 1a, scan (2026-10-23 21:31, bl616_powertest with pt_wifi, board on the bench, USB-C 1 in)

| What | Result |
|---|---|
| Radio up (RF params, lwIP, Wi-Fi task, manager ready) | **635 ms** |
| Scan, all channels | **2.93 s**, 11 APs |
| The user's AP "Vlad" | ch 6, **-29 dBm**, WPA2 (close to the bench) |
| Weakest heard | -93 dBm (several neighbours -88 .. -93 dBm): the antenna and RF receive |
| Country code | SDK default **CN** (channels 1-13, the same set as the EU here); the product sets its own |
| STA MAC | 7c:3e:82:62:b1:49 |

### Step 1b, join + DHCP (21:35, the user's AP "Vlad", credentials from `~/.leakcam_wifi`, never in the repo)

`. ~/.leakcam_wifi; pt_wifi join "$ssid" "$psk"` (console capture filtered for the key):

| What | Result |
|---|---|
| Association + WPA2 handshake | **3168 ms** (status ok, reason 0) |
| DHCP | **651 ms** |
| Join request -> address | **3.82 s** |
| RSSI / channel | -31 dBm (status: -30), ch 6 |
| Address | 10.63.220.152/24, gateway and DNS 10.63.220.238 |

### Step 1c, internet check (21:52, `pt_wifi net`: the product's wifi_check.c probe)

10 of 10: DNS connectivitycheck.gstatic.com 47 ms (first; cached after), TCP connect **33-64 ms**, `HTTP/1.1 204 No
Content` 34-73 ms later; DNS to answer **70-158 ms**. Pass.

### Step 1 current (USB-C 1 VBUS meter, K230 rails off: the BL616 alone; board on USB, cell charged)

| State | Current | Over the previous |
|---|---|---|
| BL616 ROM idle (after `isp`) | 52 mA | - |
| BL616 app awake, radio off | 62 mA | - |
| Radio up (RF, Wi-Fi task, manager), not joined | 64 mA | +2 mA |
| Scanning continuously (6 scans, 20 s) | 85-88 mA | +24 mA (receiver on) |
| Join (association + WPA + DHCP), peak | 88 mA | |
| Joined, idle | **80 mA** | +18 over radio up: no Wi-Fi power save (receiver on between beacons) |
| Internet checks in a loop | 80 mA average | TX bursts too short for the meter |

The awake BL616's 62 mA is most of it (on USB the charger's own draw is in it too; the battery figures come from the
product's sleep/wake path, test031/ENERGY.txt). For a battery session: join ~4 s at ~88 mA ~ 0.1 mAh. Power save
(DTIM sleep) when joined is a lever for long sessions: step 4 measures it with traffic.

**Step 1 pass** (scan, join + DHCP 3.8-4.0 s, internet 70-158 ms). Next: step 2, the NetHub bridge over SDIO.

Step 1 re-run on the product's code path (22:00, pt_wifi now starts the radio through firmware/bl616/wifi_link.c
`wifi_link_local_begin()`): radio up 634 ms, scan 2.93 s, join to address 4.12 s, internet 162 ms; currents 84 / 88
peak / 80 mA joined (user): the same as on the test-only path.

### Step 2, NetHub bridge (code, 2026-10-23)

- BL616: `pt_wifi bridge` (after `pt_on`): `wifi_link_start()`, the product's bridge (NetHub over SDIO, control
  channel wifi_ctrl_proto.h); the rails going off stop it first (`pt_wifi_rails_off()` in rails_off_timed()).
  wifi_link.c and the product's NetHub config are linked into bl616_powertest (1.11 MB); pt_sdu (test015's echo
  device) still links beside it, never run together.
- K230: the kernel's bl616_nethub driver (already in the image) + the `wifi` / `ifconfig` / `ping` shell commands
  switched on for this test (k230d_leakcam_defconfig; test008 had them off).
- Order: USB-C 1 in, `pt_off`, `pt_on`, `pt_wifi bridge`, `pt_run`; on the K230 console `bl616_rescan` (the agent's
  rescan by hand: nothing probes MMC0 at boot), wait for `wlan0`, `wifi scan`, then
  `wifi join <ssid> <psk>` (from `~/.leakcam_wifi`), `ifconfig`, `ping`.

### Steps 2 + 3, bridge, scan, join, ping (2026-10-23 22:25)

| What | Result |
|---|---|
| `bl616_rescan` | card bound in 42 ms (SDIO 424c:0606); netdev started (credit 8), `wlan0` with the BL616's MAC 7c:3e:82:62:b1:49, control channel up (credit 3) |
| K230 `wifi scan` over the bridge | **4.0 s**; "Vlad" -33 dBm ch 6 WPA2-AES (only that one listed, though the BL616 sent 6-8 items: open) |
| K230 `wifi join` | joined, **14.0 s**: 10 s of it RT-Thread's WLAN manager waiting for a `scan_with_info` (scan for the SSID) our driver does not answer ("scan wait timeout!"), then the join; to fix in the driver (~4 s then) |
| Address (the K230's own DHCP over the bridge) | 10.63.220.152/24, gw + DNS 10.63.220.238 (the same lease as the BL616's: same MAC) |
| ping gateway / 8.8.8.8 | both answer over `wlan0` |
| Current (USB-C 1 VBUS, user) | K230 running + bridge idle 211-221 mA, peaks 223 mA |

**Pass** for the bridge path (scan, join, DHCP, internet from the K230). Open: the join's 10 s, the scan list, then step 4
(throughput, needs a server the board can reach: this Linux VM is behind the Mac's NAT).

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test022) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| Wi-Fi TCP up / down | not measured yet | 802.11ax 20 MHz; SDIO 9.4 MB/s end to end | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | to fill | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | to fill | minimal image (rule) | the image this run used |
| Compile flags | to fill | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | to fill: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

Time from the WIFI request to associated, DHCP and NTP (paid in every networked session after the boot).

## Battery energy

Battery current: Wi-Fi join (energy per join, paid on every networked session), idle associated, sending; energy per MB uploaded.

## Obstacles

(The design obstacles and the rules they gave are collected in the skill `bl616-wifi-firmware`.)

- **The BL616 hung on the K230's first `wifi scan` and the watchdog reset it** (2026-10-23 22:10, three runs) ->
  breadcrumbs in HBN RAM (`wl_trace` in wifi_link.c, `pt_trace`, kept across the reset): the scan finished, then the
  first scan item's `nethub_vchan_user_send()` never returned. The same send from the NetHub task (status answers)
  works; from the Wi-Fi manager's event callback it blocks for ever and the watchdog feeder (top priority) starves
  too: the manager calls its callbacks with the scheduler suspended (xEventGroupSetBits still works there, which is
  why step 1's local path was fine). Fix: wifi_event() only does what cannot block and queues the code; a task
  (`wl_ev`, queue of 8) does the bridge's handling and its sends. Join events went the same way (set_state ->
  send_status), so the join would have hung too. Product code fixed with it (wifi_link.c is the product's).

- **Test firmware too small for Wi-Fi** (2026-10-23): the link failed on `vTaskSetThreadLocalStoragePointerAndDelCallback`
  (lwIP's FreeRTOS port) -> the product's FreeRTOS settings in the test firmware: 32 priorities, 100 KB heap, timer
  stack 1024, thread-local storage. Image 158 KB -> 1.06 MB.
- **No BL616 on USB after `reboot uart`, for good** (2026-10-23 21:2x, the first flash of step 1): the ROM never
  enumerated, no VMware offer, BOOT + EN no help, board alive (51 mA); after a 3V3 cut the app came back saying "reset
  by the watchdog". Cause: the new BL616 watchdog (wdg_guard.c, added the same day) kept running through the SDK's
  `reboot uart` (`bl_sys_reset_por()`) into the ROM and reset it every 8 s, before it enumerated; the user-boot
  field, kept across resets, sent each reset back into the ROM. Not the cable. Fix: `isp` command in wdg_guard.c
  (watchdog off, then the same reset), `bl616_isp.py` sends it; rule: every BL616 firmware has `isp`. The earlier
  flash with the watchdog running (16:26) passed by luck: inside the first 8 s.
