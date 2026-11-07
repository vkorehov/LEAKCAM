# test048: every metric in every check-in (low level: the BL616's sensors, the K230's STATUS, the server)

Status: **pass, automated part** (2026-11-07 09:24, run.py 34/34 on the DESIGN 5.3.11/5.3.12 firmware: the K230 on
with USB in both firmwares, `ks 2` across a wake's switch and back, the leak check's `job`/`jago`; 2026-11-06 33/33).
Steps 1, 5, 6 (reboot), 8, 9 (without the AP off), 10, 11, 13 (without a battery change), 14, 15 pass; steps 2-4
(room meter, finger, wet probe), 6's EN / watchdog / battery change, 7 and 12 need a hand or a test firmware: open.
Found: the AHT20 reads the board, not the room, on USB power: 38-44 C, 24-26 %RH on the bench (25.8 C, 53.9 %RH on
2026-10-17, test002): the BL616 die 37-38 C, the K230 die 52-56 C during a session; step 2 decides whether the
check-in's `t` and `rh` can stand for the room at all. Fixed on the way: a timed return to the full firmware cut a
running relay without recording it (`ks 0` after a relay, a4a09a9; HISTORY.md).
Hand steps 2026-11-06 22:51 (on USB power, the K230 on, the cell charging): step 2 **fails**, room 21 C against the
AHT20's 42.6 C, 26 %RH (bugs/v2.txt 12: U1 charger and the K230 suspected, ~19 mm away); step 3 passes in substance, a
fingertip on U17 raised rh 26 -> 33.5 % within seconds and lowered t 42.6 -> 41.6 C (the finger cooler than the
board), back in 25 s; step 4 (wet probe) open.

Every check-in is `POST /v1/poll?id=<id>` with a form of all the board's metrics (DESIGN.md 5.3.10): the AHT20 over
I2C, the probe node, the BL616's die and supply, the battery, uptime, boots, the reset cause, crashes, the heap, this
check-in's Wi-Fi, counters since the last good check-in and the K230's last STATUS and session. The server keeps the
last ones (`GET /v1/devices/{id}` -> `metrics`) and appends each to LEAKCAM_METRICS.

## Setup

BL616 product pair and K230 image of 2026-11-06 or later, server deployed (LEAKCAM_METRICS), bench cable on USB-C 1,
BL616 console on PR1, a stored network. A room thermometer/hygrometer next to the board for steps 2-3.

## Steps

1. **The form:** `metrics` on the BL616 console prints every key of DESIGN.md 5.3.10 (the K230 keys after a K230
   session); the next check-in's server log line and `GET /v1/devices/{id}` show the same keys, integers as numbers.
2. **AHT20:** `t` and `rh` within +-1 C / +-5 %RH of the room meter. A failed read (`rh=-1&t=0`, the check-in goes
   anyway) cannot be caused on the board (U17 is soldered on): open, checked when the server history shows one.
3. **Sensors move:** a warm finger on U17 raises `t` within two check-ins; breath raises `rh`.
4. **Probe:** dry 1600-1700 mV; a wet probe (water across S1) below 1100 mV in the next check-in.
5. **BL616 die and supply:** `tdie` 25-60 C, `vdd` 3200-3400 mV (3V3_SLEEP).
6. **Uptime, boots, reset cause:** `up` grows by the check-in interval; `reboot` -> `boots` +1, `up` small, `rst` the
   software reset's bits; a watchdog reset (`crash hang` in a test firmware) -> the watchdog bit; EN tap -> the
   external reset bit; a battery change -> `boots` 1.
7. **Crash:** after a recorded crash, `crash` = the record's count until the server took it.
8. **Heap:** `free`, `minfree`, `blocks` steady over 30 check-ins (mem_leak.py on the server history: no leak).
9. **Wi-Fi:** `rssi` within +-5 dB of the phone's reading for the same AP, `ch` the AP's channel, `scan`+`join`+`dhcp`
   within the check-in's own time (test100: join 0.24-0.47 s, DHCP ~0.6 s); the AP off for 3 check-ins (fewer than the
   presence loss): `fails` 1, 2 in the server history, then 0 after the AP is back.
10. **Counters:** `switch_to_setup wake` and back on USB power: `sw` 2 and `ks` 2 in the next good check-in (the full
    firmware's session and the setup firmware's relay, each ended by a switch: the K230 is on with USB in both
    firmwares, DESIGN.md 5.3.11), 0 in the one after; a USB plug wake in setup: `usbw` 1; a session's `kon` ~ its
    rails-on time, `klast` 1, `kms`.
11. **K230 STATUS:** in a session, `kver` = the K230 log's `version` line, `kup` its uptime, `ktdie` within +-2 C of
    msh `chip_temp`, `kheap` within 1 % of msh `free`'s available sum, `kpages` > 0; `ksago` grows between check-ins
    in the same boot; after a switch on USB it is the new boot's own session's first STATUS (0-30 s), `kago` -1.
12. **A failed session:** the K230 program killed (no heartbeat): `klast` 0, `kfail` 1.
13. **Survival:** `boots`, the counters and the K230 fields the same across a BL616 reset and the switch between the
    firmwares (HBN RAM); all reset after a battery change.
13b. **The leak check (DESIGN.md 5.3.12):** the K230's job checks at its start; the check-ins after it carry
    `job=<cam0>,<cam1>` and `jago` (s since its DONE in this boot, -1 after a reset).
14. **The server:** an unknown key is dropped; an old GET check-in is refused (405); every check-in is one line in
    LEAKCAM_METRICS.
15. **Cost:** the sensor read adds <= 80 ms before the radio starts (console timestamps), the form <= 512 B.

## Pass

Every key in every check-in with plausible values, the sensors and counters follow the board's state, the HBN values
survive resets and the firmware switch, the server keeps the last and the history; no leak, <= 80 ms added.

## Needs (build features and dependencies)

BL616: aht20.c, metrics.c (both firmwares), wifi_check.c's link figures and form hook, `.hbn_noinit.metrics`. K230:
the STATUS frame's fields, `/dev/ts`, `/dev/sysstat` (CONFIG_RT_USING_SYS_STAT). Server: the POST check-in,
LEAKCAM_METRICS.

## Metrics (2026-11-06 run; skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | console back 6.2 s after `reboot` (USB re-enumeration included); K230 READY 794 ms after the run | test022 | |
| This part's boot share | two register accesses and a counter (metrics_boot), not measurable | - | metrics_boot() at main() |
| Test run time | 538 s automated (run.py, 2026-11-06), no setup | - | |
| Sensor read before a check-in | <= 97 ms (the whole `metrics` command over the console, sampling and printing) | <= 80 ms | AHT20 + 3 ADC reads |
| Form size | 319 B metrics + ssid/version (399 B with ~80 B prefix) of 512 | <= 512 B | wifi_check.c's form buffer |
| Current idle / during | n/a: on USB power, not metered | floor (test031) | |
| Memory BL616 / K230 (start / min / end), leak | free 193972..194104 B, minfree 188044 B over 7 check-ins; K230 heap 7002240 B, min 6992384 B, pages 46583808 B | no leak | over >= 8 check-ins |
| Image (sizes) | BL616 test firmware 1326272 B (2026-11-06, on the board at the run) | minimal image (rule) | |
| Speed / rate against the top | check-in Wi-Fi scan 629 + join 251 + DHCP 50 ms (test100: join 0.24-0.47 s); cable setup COMMIT -> SAVED 1.39 s | SPEEDS.txt | |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: on USB power, not metered | - | the sensor read ~70 ms awake per check-in (metrics.h) |
| Per day | n/a: not metered | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | BL616 die 37.0 / 38.5 / 38.3 C; board (AHT20) 37.9 / 43.9 / 41.4 C; K230 die 45.1 / 55.7 / 55.7 C in the relay | K230 limit (datasheet) | |
| Compile flags | BL616 product -Os -g (SDK Kconfig default), no LTO; K230 program -Os -march=rv64imafdcv, sections + --gc-sections, strip (as test113) | smallest code (rule) | |
| Features over the minimal image | metrics.c, aht20.c (BL616 both firmwares); /dev/sysstat, /dev/ts reads (K230) | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |
