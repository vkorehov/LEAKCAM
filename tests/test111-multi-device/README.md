# test111: more than one LEAKCAM: one device ID everywhere, presence through the server

Status: **pass, 28/28** (2026-11-06 09:20, run.py, 150 s; product firmware with the cable setup): one device ID (7C3E8262B149) on the board, USB and BLE; the app
opens on its status screen, sets up a known board through it with the short "change Wi-Fi" flow, shows a stored network
as hidden and stores none; the board online at the server after a setup; a second LEAKCAM online, offline, deleted.

Each LEAKCAM is known by its device ID, the BL616's factory MAC (device_id.c): its USB serial number, its BLE
advertisement (manufacturer data, company 0xFFFF) and its server check-ins (`POST /v1/poll?id=`) carry the same 12 hex
digits. The app remembers the LEAKCAMs it set up and shows above the steps whether each is online, from the presence
server (`GET /v1/devices/<id>`). From boot the board checks in at the interval the server's answer gives (`checkin`: product default 3600 s, the
bench server 60 s; per device by `PUT /v1/devices/<id>/intervals`, DESIGN.md 5.3.12) whenever a network is stored;
sleeping in HBN between check-ins on battery is a later step (test132).

## Setup

- BL616 full firmware with presence (firmware/bl616_full); the setup app (android/); the presence server on the Pi
  (vhome 192.168.1.159:8090, systemd unit leakcam-server, `server/deploy.sh`).
- The phone on adb, unlocked; USB-C 1 in; the test network from `~/.leakcam_wifi`.
- A second LEAKCAM simulated: an ID no board has (02AB12CD34EF) put into the app's list (debug build: run-as), checked
  in at the server from the bench.
- The runner sets the board's check-in interval for the run (10 s) per device with `PUT /v1/devices/<id>/intervals`
  (phone.intervals), reads `check-in every N s` from the board's `presence`, and sets 60 s back at the end.

## Needs (build features and dependencies)

As test106, plus eFuse read (the device ID), the presence task (wifi_check.c run with the server as target), the
app's INTERNET permission, the server's per-device intervals (`PUT /v1/devices/<id>/intervals`).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. **One ID:** `presence` on the board, the USB serial number (udev ID_SERIAL_SHORT) and the ID the app read from the
   advertisement during a setup are the same.
2. **Check-in:** after the setup the server shows the board online within two intervals; the board's `presence` says
   its last check-in reached the server (state 0x03).
3. **App:** reopened, it lists the board as online.
4. **Second LEAKCAM:** listed as "not seen yet"; online after one check-in from the bench; offline after three intervals
   without; the board online throughout.
5. **Status screen and short flow:** with LEAKCAMs remembered the app opens on its status screen and sets up the board
   on USB power through it; a known board gets "Enter the new Wi-Fi network for ..."; the advertisement flags a stored
   network; the app's storage holds IDs and names only, no network. **Delete:** the second LEAKCAM removed from the
   list, the board still listed.

7. **Delete, the only user (user, 2026-11-04; DESIGN.md 5.3.5):** before it the server lists this phone as the board's
   user (registered when it issued the PIN, by Android ID; GET /v1/devices/<id> "phones"), after it none (the delete
   goes to the server first, the app's list only after it confirmed); the board deleted in the app stays out of the list,
   also after the app is reopened (no "rediscover"); with no phone left the server answers its check-in `410 Gone`
   and the board is back in the setup firmware within one check-in interval; set up again from the app afterwards.
   **Not built yet** (the app's delete to the server first, the server's phones per board, the full firmware's 410): fails until
   then.
8. **Battery at the server (DESIGN.md 5.3.9):** each check-in carries `bat`/`chg`; `GET /v1/devices/{id}` answers
   `battery_mv` (the board's `battery`, -1 none) and `charging`; the app's row shows it out of BLE range (test131).

## Setup mode (DESIGN.md 5.3.1, planned)

- In setup mode (once test120 passed): a board without presence shows red in the app and is reachable over BLE from
  low power (Configure enabled), one with presence is woken through the server (test119).

## Pass

Every channel shows the same ID; the app shows each of its LEAKCAMs online or offline as the server sees it.

## Result

run.py, 2026-11-01 11:24, product firmware (presence from boot, flags byte in the scan response) + the setup app with
the status screen + server/ on the Pi (check-in every 10 s for the run, 60 s after): 18/18; crash record none before and after.

- Step 1: `presence` id 7C3E8262B149 = USB serial number = the ID the app read from the scan response; the MAC the router
  lists for the board.
- Step 5: the app opened on its status screen; it found the board on USB power and opened its setup in 1.2 s; the short
  flow ("Enter the new Wi-Fi network for LEAKCAM-B14A"); setup Done 7.9 s; the scan response flagged a stored network
  (creds 1); the app's storage held IDs and names only.
- Step 2: online at the server 42 s after the setup (the board on the bench server's 60 s until the first answer gave 10 s);
  last check-in state 0x03.
- Steps 3-4: the board online in the app; the second LEAKCAM "not seen yet", "online" after one check-in, "offline, last
  seen 34 s ago" after three intervals; deleted from the list with the board still listed and online.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: none (on USB).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the BL616 alone, K230 off | target (test022) | longest step: |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | 137 s automated (run.py, 2026-11-01), no setup | - | setup (flashing, plugging, waiting) separately |
| presence: check-in to online in the app | setup Done -> online at the server 39 s (check-in every 10 s, the board's first interval 60 s until told) | - | % of the top |
| Current idle / during (min / avg / peak) | n/a: not metered; a check-in is the BLE check's join + HTTP on USB (test040: check peak 86 mA) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | n/a: on USB power only | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a for the cell: on USB | - | scripts/energy.py |
| Per day | n/a: on USB; the battery check-ins are test108 | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | n/a: the product firmware has no sensor command yet (AHT20 only in the test firmware: test040/test106 board 28-33 C on the same USB setup) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 146072 / min 120144 / 146072 B, blocks 105 -> 105, deleted 0; leak: n/a, 1 snapshots (mem_leak.py needs >= 8 cycles) (2026-11-01) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free` |
| Image (sizes) | BL616 test firmware 1317888 B (2026-11-01, on the board at the run) | minimal image (rule) | the image this run used |
| Compile flags | BL616 product -O3 + LTO (product defconfig), sections, --gc-sections | smallest code (rule) | |
| Features over the minimal image | as test106, plus presence.c and device_id.c | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

n/a: no K230 boot.

## Battery energy

n/a: on USB power; presence on battery is test108.
