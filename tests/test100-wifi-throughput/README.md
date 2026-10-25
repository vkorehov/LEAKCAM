# test100: Wi-Fi throughput, K230 -> SDIO -> BL616 -> AP

Status: **steps 1-4 pass** (2026-10-25): BL616 alone (scan, join, internet); bridge (scan, join, DHCP, ping); K230 card bound -> address 0.33-0.59 s after the SDIO card-interrupt and DHCP ARP fixes (was 5.05 s); TCP up 17.1 / down ~26 Mbit/s; BL616-only server check 1.71 s awake. Open: the upload limit, step 5 (two RTMP streams, needs the cameras), the metrics table.

The product path for clips and checks: the K230's lwIP over the NetHub SDIO bridge (test015: bus 21 MB/s, not the
limit) to the BL616's 2.4 GHz radio. Needs the product BL616 firmware (open on rev 1) and stored credentials
(`wifi_set` on the debug console, or BLE).

## Setup

**MISSING**: hookup, firmware / image, and the probes named in Metrics.

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

### Robustness + leak loop, 2026-10-24 (after the fixes; verdict from the BL616's counters, not console text)

`wloop2.py`: bridge up, 20 K230 `wifi scan`, then 8 x (`wifi disc`, `wifi join`); BL616 `mem` per cycle; at the end
`pt_trace counts` (every wl_trace code counted since boot).

| What | Result |
|---|---|
| K230 scan requests / scans run / finished | 28 / 28 / 28 (20 by hand + 8 by RT-Thread's join), 232 items, **0 failed sends** |
| Joins / connect called / returned / link up | 8 / 8 / 8 / 8: **no hung connect** |
| Lost K230 messages, lost events, leaves without DISCONNECT | 0, 0, 0 |
| BL616 memory, 20 scans | free 194748 -> 194748 B, blocks 84, deleted 0: **no leak** |
| BL616 memory, 8 joins | 11 KB once at the first join (station buffers), then 183528 -> 183544 B, blocks 119, deleted 0: **no leak** |

**Rerun 2026-10-24 ~10:15 with the K230 console by UART interrupt:** bridge PASS again (27/27 scans, 7/7 connects,
0 lost), BL616 `rx dropped 0`, K230 heap 6982656 -> 7269504 B free (max used 1.56 MB): no leak on either chip. By
hand then: `wifi disc` **1.1 s** (was 16-31 s), `wifi join` **9.1 s** (scan 3 s + join; was 14 s with the 10 s
scan wait), link-up RSSI -29 dBm (was 0), an IP every time. The loop's two odd joins (no IP, one join not sent)
were its fixed 5 s / 15 s waits: the runner now waits for the prompt and up to 15 s for DHCP.

**Pass** for the bridge. The K230 console lost bytes during the run ("ip 10.63.22", tables cut): the console bridge
(k230con) polls the UART at idle priority since the IDLE fix and misses the 32-byte FIFO at 4 Mbaud when the Wi-Fi
tasks run -> UART receive by interrupt (next).

### K230 time to an address, 2026-10-25 (`k230_join_timeline.py`; why the join took 14 s / 9.1 s)

Product path: `pt_wifi bridge` (the BL616 joins from its stored credentials, remembered AP: 0.24-0.47 s), K230 boot,
`bl616_rescan` (the agent's WIFI step by hand: the powertest firmware does not answer the agent's READY), then
`ifconfig` polled until wlan0 has an address. Test path: msh `wifi disc`, `wifi join`. Every K230 console line is
timestamped by a reader thread; `bl616_stat` (new) counts card interrupts against the driver's 1 s safety polls.

| Step | Before | Card interrupt armed | + no DHCP ARP check (3 runs) |
|---|---|---|---|
| card bound -> netdev/vchan started | 1.000 s | 11 ms | 11 ms |
| started -> link UP (BL616 already joined) | 1.000 s | 0 ms | 0-11 ms |
| link UP -> `Got IP` | 2.80 s | 1.30 s | **0.10-0.40 s** |
| **`bl616_rescan` -> address (product)** | **5.05 s** | 1.38 s | **0.33-0.59 s** |
| msh `wifi join` -> address (test path) | 10.9 s | 3.96 s | 3.43 s (2.94 s of it RT-Thread's own scan of every channel) |
| `bl616_stat` card irqs / safety polls / late | 0 / 15 / 12 | 28 / 4 / 0 | 29-32 / 3-4 / 0 |

**The hidden issue: the SDIO card interrupt never fired.** RT-Thread's `sdio_attach_irq()` enables the function's
interrupt in the card (CCCR) but never the host's card interrupt (Linux does on the first claim), and the K230 SDHCI
arms only the signal-enable half at init. So no BL616 -> K230 message ever woke the driver: each waited for the 1 s
safety poll or for a K230 transmit that happened to wake the worker (the DHCP OFFER and ACK, every link status, every
scan item). Fix in install.sh (RT-Thread SDIO core): arm the host card interrupt when the first function IRQ is
attached. The 14 s / 9.1 s joins and the step-2 scan's 4.0 s carried it, and the throughput of step 4 would have.
Then lwIP's ARP probe of the offered address (500 ms timeouts on the 500 ms DHCP timer) held the address back by up to
1 s: off (install.sh, lwipopts.h); the lease is the BL616's own, same MAC.

The test path's remaining 2.94 s is RT-Thread's WLAN manager scanning before every `rt_wlan_connect()`; the product
never calls it (the BL616 joins by itself), so it stays.

Current during the runs (USB-C 1 VBUS, user, run before the fix): **220 mA average, 243 mA peak**.

### Step 4, TCP throughput on the home router "VK" (2026-10-25 10:13, ch 11, RSSI -47 dBm, board on the bench)

`throughput.py` (K230 RT-Thread `iperf`, PC 192.168.1.131 wired, `pc_tcp_bench.py`); remembered-AP join, K230
address 3 s after `pt_run`.

| What | Result |
|---|---|
| Upload K230 -> PC, 10 s | **17.1 Mbit/s** (12.1-19.7 per second) |
| Download PC -> K230, 14 s | **~26 Mbit/s** (18.3-31.2 per second) |
| `bl616_stat` | card irqs 17212, safety polls 20, late 0 |
| BL616 memory, one run | free 177544 -> 177252 B, blocks 128 -> 141, deleted 0 (one run: no leak verdict) |
| K230 memory | **MISSING**: the runner's `free` parse found nothing |

**Pass** against the plan: two H.265 streams (~1 Mbit/s with audio) use ~6 % of the upload.

### BL616-only server check (`pt_poll`, 2026-10-25 10:20; DESIGN 5.1 "Live view on request")

Test firmware `pt_poll <n> <s>`: every wake from HBN runs radio up, join at the remembered AP, DHCP, DNS + TCP + HTTP
(connectivitycheck.gstatic.com, standing in for `GET /v1/poll`), then HBN `<s>` again. 3 of 3 cycles ok:

| Step | Mean |
|---|---|
| radio up (RF params, lwIP, Wi-Fi task, manager) | 642 ms |
| association + WPA (remembered AP) | 240 ms |
| DHCP (BL616's lwIP) | 747 ms |
| DNS + TCP + HTTP 204 | 49 ms |
| **awake, core reset to HBN** | **1709 ms** |

Energy, from these times and step 1's currents (the USB meter cannot follow a 1.7 s burst; it showed 80 mA = joined
idle, the board after the last cycle): **~0.036 mAh per check**, so ~3.4 mAh/day every 15 min, ~10 mAh/day every 5 min
(ENERGY.txt). Radio up and DHCP are 81 % of the awake time: the levers.

## Metrics (skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the powertest firmware does not answer the agent's READY; K230 pt_run -> msh 1.95-1.97 s (3 runs, timeline); READY in test022 | target (test022) | longest step: |
| This part's boot share | card bound -> address 0.33-0.59 s (3 runs) after the K230 is up; was 5.05 s | - | |
| Test run time | timeline ~25 s, throughput ~65 s, pt_poll 3 cycles ~25 s; setup: K230 flash 2 min, BL616 flash 1 min each | - | setup (flashing, plugging, waiting) separately |
| Wi-Fi TCP up / down | 17.1 / ~26 Mbit/s (12-20 / 18-31 per second) | 802.11ax 20 MHz; SDIO 9.4 MB/s end to end | % of the top |
| Current idle / during (min / avg / peak) | K230 + bridge during the join runs: 220 avg, 243 peak mA (user, VBUS, before the fix); BL616 alone joined 80 mA | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | n/a: on USB power (charger rail), not probed this run | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | BL616-only server check ~0.036 mAh (1.71 s awake x step 1 currents) | - | scripts/energy.py |
| Per day | checks every 15 min: ~3.4 mAh/day (ENERGY.txt) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | board 31.2 C (AHT20, pt_status, before the runs); die n/a: chip_temp not read on these runs | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 177544 -> 177252 B, blocks 128 -> 141, deleted 0 (one throughput run); K230 n/a this run (the runner's `free` parse found nothing); leak loops of 2026-10-24: no leak on either chip | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | K230 kdimg 45436928 B (iperf in); BL616 test firmware 1114848 B | minimal image (rule) | the image this run used |
| Compile flags | SDK defaults, unchanged by this test | smallest code (rule) | |
| Features over the minimal image | test-only: msh wifi/ifconfig/ping, bl616_rescan/bl616_stat, iperf (none in the product image); size not measured | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: the rate is bound by the link and the radio (CPU idle); revisit in the upload-limit work | the faster one stays | |

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

- **K230 scan list short (1 of 6-8 APs) and its join waiting 10 s** (2026-10-23/24) -> instrumented both ends (BL616
  `wl_trace` 0x33/0x34 per failed send, K230 "scan done: n received, the BL616 sent m"): the BL616's upload pool holds
  5 messages and `nethub_vchan_user_send()` waits only 100 ms for one; items 6+ waited 69 ms in a good scan and were
  dropped in back-to-back scans, the DONE with them (so RT-Thread's join waited out its 10 s). Fix: `vchan_send()`
  retries for up to 2 s; one scan at a time (`scan_pending`: a request during a scan is answered by its DONE).
- **After join / leave / join every K230 scan failed "scan sync fail"** (2026-10-24) -> BL616 `virtualchan status`:
  2 K230 messages waiting, unprocessed; the trace: the JOIN's `wifi_mgmr_sta_connect()` never returned, and it ran in
  the NetHub virtual-channel task (`ctrl_rx`): nothing consumed, no credit back, the K230's channel stopped. Fix:
  `ctrl_rx` only queues (struct wl_item) and the worker handles the message; a join leaves first and waits for the
  DISCONNECT (own leaves counted, so their late events do not fail the next join).
- **The connect still hung, now in the worker** (2026-10-24) -> narrowed on the BL616 alone (join/leave/join with and
  without autoconnect, with a scan between, with `use_dhcp=0`: all fine, so bridge-only), finer marks (0x62 connect
  called, no 0x63), then `ps`: `wifi_mgmr`, `CONNECT_ENTRY`, `CONNECT` in state D (deleted, not freed) and `k230con`
  always R. The console bridge polled the UART and only `taskYIELD()`ed at priority 1 whenever the K230 was on, so
  IDLE (priority 0, which frees deleted tasks) never ran; the manager's per-connect tasks piled up until a connect
  could not get its tasks. Fix: the bridge no longer busy-polls (UART receive by interrupt, below). Seen in `mem` as
  `deleted` > 0 that stays: why every test now records memory (tests/README.md, `mem_leak.py`).
- **K230 console lost bytes during Wi-Fi runs** ("ip 10.63.22", cut tables; 2026-10-24) -> the first IDLE fix put the
  polling bridge at idle priority, where it missed the 32-byte RX FIFO (80 us at 4 Mbaud) while the Wi-Fi tasks ran.
  Fix: UART RX interrupt into a 4 KB ring, the bridge task blocks on it (priority 5); `pt_status` shows `rx dropped`
  (0 since).
- **The K230 got its address 5 s after the card bound, with the BL616 already joined** (2026-10-25) -> two exact
  1.000 s steps and a 2.8 s DHCP: the SDIO card interrupt was never armed on the host (RT-Thread SDIO core), so the
  driver ran on its 1 s safety poll (`bl616_stat`: card irqs 0) -> install.sh arms it at the first IRQ attach (0.33-0.59
  s since, with the DHCP ARP check off).
- **My first timeline runs measured nothing** (2026-10-25) -> (1) wlan0 never came up: the agent's WIFI step (the
  rescan) waits for READY, which the powertest firmware does not answer -> the runner sends `bl616_rescan`; (2) the
  whole boot log had one timestamp: the console was read only after `pt_run` returned (1.5 s wait) -> a reader thread
  from before the reset.
- **The first `pt_poll` cycles did not repeat** (2026-10-25) -> on USB power the RTC wake left no HBN interrupt state
  ("cold start / reset") -> the loop resumes on pt_sleep's own wake mark; the first cycle also starts from a sleep
  (it had run in the open session: radio already up, 209 s "awake").
- **The PC's firewall blocked port 5001** (2026-10-25) -> `netsh` from the MCP's Python needed elevation -> added
  with `Start-Process -Verb RunAs` (no prompt on that PC): rule "LEAKCAM TCP bench 5001", local subnet only.
- **The SDK-level defconfig select failed** (`leakcam_docker_build.sh k230d_rtos_leakcam_defconfig`: Kconfig.app
  names the removed memtest app) -> not needed: install.sh copies the kernel defconfig and `log` builds with it
  (iperf.o compiled).
- **The PC sender aborted at the end** (ConnectionAbortedError) -> the runner stops the K230's iperf after 25 s while
  the PC still sends: the per-second figures before it are valid.
- **First DHCP lease from the home router took 4.0 s** (BL616, quick join), 0.75 s on the following polls: the
  router's first answer, not ours.
- **Step 4 on the home router "VK" (2026-10-25): no association** -> the AP rejected the key (`auth_failures=1
  reason=WRONG_KEY`; VK seen ch 11, -48 dBm, WPA2; `~/.leakcam_wifi` holds exactly the 10 characters given) -> waiting
  for the right key from the user -> the second key given worked.
- **My throughput runner gave up on the address after 4 s** (2026-10-25): 60 `ifconfig` tries that each returned at
  once -> a 30 s deadline.
- **Link-up status said 0 dBm** -> the RSSI read at the connect event is 0 -> the status is sent again 1 s later
  (-29 dBm).
- **My loop runner misjudged the bridge three times** (2026-10-24) -> (1) every command "took" 0.0 s: the leftover
  prompt matched before the command ran -> drain the console before each command; (2) "NO DONE LINE" with a working
  bridge: the K230 kernel log and the shell table interleave character by character, the text match failed -> the
  verdict comes from the BL616's counters (`pt_trace counts`) and `mem`, the console only for timing; (3) joins
  without an IP: fixed 5 s / 15 s waits shorter than a join with its scan and DHCP -> wait for the prompt, then up to
  15 s for DHCP.
- **`isp` did not reach the ROM 5 times of ~20** (2026-10-23/24), each time after the BL616 had run a while, a 3V3 cut
  fixed it -> USB detach before the reset added (`isp`: `usbd_deinitialize`, 0.5 s); then the user turned VMware's USB
  popups off (devices attach to Linux by themselves): missed or held prompts are the likely cause. Watch: one more
  miss now would be a firmware fault (then: the app writes its own update, no ROM).
