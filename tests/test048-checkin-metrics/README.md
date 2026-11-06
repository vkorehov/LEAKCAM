# test048: every metric in every check-in (low level: the BL616's sensors, the K230's STATUS, the server)

Status: **planned** (2026-11-06): built (BL616 pair, K230 image, server), not run.

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
6. **Uptime, boots, reset cause:** `up` grows by the poll interval; `reboot` -> `boots` +1, `up` small, `rst` the
   software reset's bits; a watchdog reset (`crash hang` in a test firmware) -> the watchdog bit; EN tap -> the
   external reset bit; a battery change -> `boots` 1.
7. **Crash:** after a recorded crash, `crash` = the record's count until the server took it.
8. **Heap:** `free`, `minfree`, `blocks` steady over 30 check-ins (mem_leak.py on the server history: no leak).
9. **Wi-Fi:** `rssi` within +-5 dB of the phone's reading for the same AP, `ch` the AP's channel, `scan`+`join`+`dhcp`
   within the check-in's own time (test100: join 0.24-0.47 s, DHCP ~0.6 s); the AP off for 3 check-ins (fewer than the
   presence loss): `fails` 1, 2 in the server history, then 0 after the AP is back.
10. **Counters:** `switch_to_setup wake` and back: `sw` 2 in the next good check-in, 0 in the one after; a USB plug
    wake in setup: `usbw` 1; a K230 session (setup relay): `ks` 1, `kon` ~ its rails-on time, `klast` 1, `kms`.
11. **K230 STATUS:** in a session, `kver` = the K230 log's `version` line, `kup` its uptime, `ktdie` within +-2 C of
    msh `chip_temp`, `kheap` within 1 % of msh `free`'s available sum, `kpages` > 0; `ksago` grows between check-ins
    in the same boot and is -1 after a reset.
12. **A failed session:** the K230 program killed (no heartbeat): `klast` 0, `kfail` 1.
13. **Survival:** `boots`, the counters and the K230 fields the same across a BL616 reset and the switch between the
    firmwares (HBN RAM); all reset after a battery change.
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

## Metrics (**MISSING**: not run; skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | **MISSING** | test022 | |
| This part's boot share | **MISSING** | - | metrics_boot() at main() |
| Test run time | **MISSING** | - | |
| Sensor read before a check-in | **MISSING** | <= 80 ms | AHT20 + 3 ADC reads |
| Form size | **MISSING** | <= 512 B | wifi_check.c's form buffer |
| Current idle / during | **MISSING** | floor (test031) | |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak | over >= 8 check-ins |
| Image (sizes) | **MISSING** | minimal image (rule) | |
| Speed / rate against the top | **MISSING** | SPEEDS.txt | |
