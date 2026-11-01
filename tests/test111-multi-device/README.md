# test111: more than one LEAKCAM: one device ID everywhere, presence through the server

Status: **pass, 12/12** (2026-11-01 09:52, run.py 137 s): one device ID (7C3E8262B149) on the board, USB and BLE; the
board online at the server 39 s after a setup; the app shows it and a second LEAKCAM online and offline as the server
sees them.

Each LEAKCAM is known by its device ID, the BL616's factory MAC (device_id.c): its USB serial number, its BLE
advertisement (manufacturer data, company 0xFFFF) and its server check-ins (`GET /v1/poll?id=`) carry the same 12 hex
digits. The app remembers the LEAKCAMs it set up and shows above the steps whether each is online, from the presence
server (`GET /v1/devices/<id>`). On USB power the board checks in every poll interval (the server's, 60 s by default);
on battery a later use case (the HBN scheduler, test108).

## Setup

- BL616 product firmware with presence (firmware/bl616); the setup app (android/); the presence server on the Pi
  (vhome 192.168.1.159:8090, systemd unit leakcam-server, `server/deploy.sh`).
- The phone on adb, unlocked; USB-C 1 in; the test network from `~/.leakcam_wifi`.
- A second LEAKCAM simulated: an ID no board has (02AB12CD34EF) put into the app's list (debug build: run-as), checked
  in at the server from the bench.

## Needs (build features and dependencies)

As test106, plus eFuse read (the device ID), the presence task (wifi_check.c run with the server as target), the
app's INTERNET permission, ssh to the Pi for the poll interval.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. **One ID:** `presence` on the board, the USB serial number (udev ID_SERIAL_SHORT) and the ID the app read from the
   advertisement during a setup are the same.
2. **Check-in:** after the setup the server shows the board online within two intervals; the board's `presence` says
   its last check-in reached the server (state 0x03).
3. **App:** reopened, it lists the board as online.
4. **Second LEAKCAM:** listed as "not seen yet"; online after one check-in from the bench; offline after three intervals
   without; the board online throughout.

## Pass

Every channel shows the same ID; the app shows each of its LEAKCAMs online or offline as the server sees it.

## Result

run.py, 2026-11-01 09:52, product firmware with presence + the setup app + server/ on the Pi (poll 10 s for the run,
back to 60 s after): 12/12, 137 s; crash record none before and after.

- Step 1: `presence` id 7C3E8262B149 = USB serial number (udev ID_SERIAL_SHORT) = the ID the app read from the BLE
  advertisement; it is the MAC the router lists for the board (byte order as eFuse gives it).
- Step 2: online at the server 39 s after the setup's Done (the board still on its 60 s default until the server's first
  answer gave it 10 s); its `presence`: last check-in state 0x03 (reached the server).
- Step 3: the app reopened: "LEAKCAM-B14A: online".
- Step 4: the second LEAKCAM "not seen yet", "online" after one check-in from the bench, "offline" 32 s after it stopped
  (three intervals); the board online throughout. ("last seen 0 min ago" under a minute: now seconds below a minute.)

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: none (on USB).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the BL616 alone, K230 off | target (test022) | longest step: |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | 137 s automated (run.py, 2026-11-01), no setup | - | setup (flashing, plugging, waiting) separately |
| presence: check-in to online in the app | setup Done -> online at the server 39 s (poll 10 s, the board's first interval 60 s until told) | - | % of the top |
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
