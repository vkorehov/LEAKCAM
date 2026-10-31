# test042: RF performance against the bench phone (RSSI side by side)

Status: **planned** (2026-10-31): runner written (run.py); waits for the board on USB and the phone on adb.

No RF instruments on the bench (no VNA, no spectrum analyser): the board's antenna and radio are judged against a
known-good receiver and transmitter, the bench phone, at the same spot. The board's 2.4 GHz antenna (simulated, not
tuned on the board) serves Wi-Fi (clips, checks) and BLE (setup).

## Setup

- Board on the bench in the open, USB-C 1 in (BLE advertises only on USB power), K230 off; BL616 test firmware with
  `pt_wifi` and `pt_ble`.
- The bench phone (SM-G715FN) lying flat right beside the board (same spot, a few cm apart), on adb, unlocked,
  Bluetooth and Wi-Fi on; skill android-bench-phone.
- The access points around (the home router "VK" ch 11 and the neighbours'): fixed transmitters on several channels.

## Needs (build features and dependencies)

BL616: Wi-Fi manager scan (`pt_wifi scan`: SSID, channel, RSSI, BSSID per AP), BLE advertising (`pt_ble on`); phone:
`cmd wifi start-scan` / `list-scan-results` over adb, the instrumented BleOp `scan` (test040).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. **Wi-Fi receive per channel:** `run.py "<orientation>"`: 10 scans each by the board and the phone; per access point
   (BSSID, channel) the median RSSI of each and board - phone; median of the difference per channel. The phone's
   antenna is the reference, so the difference is the board's antenna + front end against a phone's.
2. **Orientations:** the board flat (antenna to 4 sides), on edge, upside down: the antenna's directivity; the phone
   stays where it is.
3. **BLE transmit:** in the same runs, the phone's RSSI of the board's advertising (10 scans, median).
4. **Distance:** the phone at ~3 m (and the install distance, another room): BLE and the board's RSSI of the home AP.
5. **Per channel, transmit** (later): the phone as a hotspot on channels 1 / 6 / 11 if its build lets adb pick the
   channel (`cmd wifi start-softap`); the board joins each, RSSI both ways and throughput (test100's iperf).
6. **BLE per advertising channel** (later): the test firmware advertising on 37, 38 or 39 alone (2402 / 2426 /
   2480 MHz: bottom, middle, top of the band).

## Pass

Board - phone within about -10 dB on every channel seen, in the worst orientation; the board joins and holds the home
AP at the install distance; the phone hears the board's BLE at 3 m in every scan. (Starting figures: a phone's antenna
is a few dB better than a small PCB antenna; worse than -10 dB points at the antenna or its match.)

## Result

Not run yet.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median / max of 5) | n/a: the K230 stays off; the BL616 radio only (2026-10-31) | target (test022) | longest step |
| This part's boot share | n/a: no boot step (2026-10-31) | - | driver / mount / probe |
| Test run time | **MISSING** | - | setup separately |
| Speed / rate against the top | **MISSING** | SPEEDS.txt | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 VBUS meter |
| Voltage under load (min) | n/a: on USB power, the charger rail (2026-10-31) | cutoff 3.10 V (bug-004) | probe points named |
| Power, energy per operation | **MISSING** | - |  |
| Per day | n/a: a measurement, not a product operation (2026-10-31) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; mem_leak.py over >= 8 cycles |
| Image (sizes) | **MISSING** | minimal image (rule) | the image this run used |
| Compile flags | **MISSING** | smallest code (rule) |  |
| Features over the minimal image | n/a: test firmware commands only (2026-10-31) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays |  |

## Boot time

n/a: the K230 stays off; the BL616's Wi-Fi start is test100's.

## Battery energy

n/a: a measurement of the radio path; the radio's energy per session is test100 / test102.
