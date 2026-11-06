# test131: the app's battery icon from every channel (high level, product firmware + app + server)

Status: **partly run** (2026-11-06): steps 2 and 6 pass for a board with no reading yet, server only (online over
Wi-Fi, not in BLE range, on USB power): the row shows the outline with the green tip, tooltip "Charging (USB power) ·
battery not measured yet" (`row ... bat -1 chg 1`). First look: no icon at all, the app drew it only with a level or
in BLE range though the server said `charging`; fixed (the server's power alone shows the outline). Steps 1 and 5 pass
(22:40-22:42, full firmware 0.6+ga2fa289): USB-C 1 out -> the K230's reading at the unplug (3815 mV) at the server 3 s
later (the check-in at once), the app's row grey, half filled, "Battery 50 %"; USB-C 1 in -> `charging` at the server
27 s later (the next 60 s check-in; since 98b2118/98ca601 the plug checks in at once: unplug -> server 2 s, plug ->
server at once, the K230's relay device back 3 s later, 22:48-22:49), the row green, half filled, "Charging (USB power) · 50 % when last on battery".
Steps 3-4 (BLE only, the USB cable's ID line) open.

The app shows one battery icon per LEAKCAM: filled to the charge, green while it is on USB power (charging), red at
15 % and below, only the outline while no level is known; the percent on the icon's tooltip (hover, long press). The
level is the K230's last reading on battery, reported by the board on every channel (DESIGN.md 5.3.9): the server
(check-in `bat=`/`chg=`, `GET /v1/devices/{id}` `battery_mv`/`charging`), the BLE advertisement (byte 10, 20 mV
steps) and the USB cable's `ID` line. The app logs each row: `row <id> ... bat <percent, -1 none> chg <0|1>`.

## Setup

Bench phone (adb over Wi-Fi), the app, the server on the Pi, product firmware on both chips, a network stored.

## Steps

1. A board with a reading (after a session on battery, test046 step 2), on USB power, set up (online): the status
   screen's row shows the icon green and filled to the percent of `battery` on the console; the tooltip says
   "Charging (USB power) · <n> % when last on battery".
2. Server only (BLE off on the phone or out of range): the row's level and charging from `GET /v1/devices/{id}`.
3. BLE only (the board in the setup firmware, advertising; Wi-Fi of the phone off): the level from the
   advertisement byte; the server's value not used.
4. USB cable (test113's flow): the cable screen's board has the level and green from its `ID` line.
5. On battery (hand: USB-C 1 out, a check-in after it): grey (or red at <= 15 %) and "Battery <n> %".
6. No reading yet (HBN RAM lost: battery out and in, or a fresh board): only the outline, "not measured yet".
7. The percent from the voltage: 4.20 V -> 100, 3.80 V -> 48, 3.10 V -> 0 (BatteryIcon.percent; a unit check on the
   host).

## Pass

The same level and charging state on all three channels; the icon's fill, colour and tooltip match the board's
`battery` and its power; no icon claims a level the board never measured.

## Needs (build features and dependencies)

App: BatteryIcon.kt, the three sources in MainActivity.kt. Server: check-in `bat`/`chg`, device `battery_mv`/
`charging`. BL616: battery.c, the advertisement byte, the USB `ID` fields. K230: test046.
