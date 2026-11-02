# test127: the setup firmware hands over to the full firmware on a USB plug, only with the network checked to the API

Status: **pass, steps 1-6** (2026-11-02, user: "usb plug, not only with stored network, but also with checked and once API
reachable, API reachable should be end goal of network test"). Design: DESIGN.md 5.3.1 (Setup -> full).

On top of test121 (the plug wakes the sleeping setup firmware) and test126 (the slot switch): the setup firmware
runs the network check on the stored network (join -> address -> internet -> a check-in answered by the LEAKCAM API,
api.h) and switches to the full firmware only when it passes (firmware/bl616_setup/switch_to_full.c). Any other outcome
keeps the setup firmware, awake on USB power and advertising, with the failed step on the console. New credentials
stored over BLE (checked the same way) switch once the phone has read SAVED and left.

## Setup

- The pair flashed: the full firmware in slot 0, the setup test bed in slot 1 (`make -C firmware/bl616_full flash
  SLOT1_BIN=../../tests/firmware/bl616_setup_test/build/build_out/bl616_setup_test_bl616.bin`), then `slot switch`
  into the test bed, `lp_boot`, `switch_to_full on`.
- The presence server (server/) on the Pi; the network stored from ~/.leakcam_wifi (never on a command line).
- USB-C 1: unplugged and plugged by hand for steps 1-2 (one plug each); the other steps use `switch_to_full usb` (the
  same code as the plug, without the hand).

## Needs (build features and dependencies)

BL616: switch_to_full.c, slot.c, wifi_check.c with the API step (api.c, WCHK_NO_API 0x19), the PDS IO wake (test121), BLE
(ble_pairing.c). Server: GET /v1/poll. App: the "API not reachable" text (0x19).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Asleep (`wakeup_timer 120000`, USB out), a network stored and the server up: USB-C 1 plugged in -> the check
   passes -> the full firmware answers (`slot`: full, active 0); times: plug -> wake, check, switch, full console.
2. The same with the server stopped (`systemctl stop leakcam-server` on the Pi): the check fails at the API
   ("state 0x19"), the setup firmware stays (`slot`: setup_test), awake, advertising (phone scan finds it).
3. `switch_to_full usb` with a wrong passphrase stored (`pt_ble` store unchecked): fails at the join (0x11), stays.
4. `switch_to_full usb` with no network stored (`pt_ble erase`): "no network stored: setup stays".
5. BLE setup (BleOp session, the app's check): SAVED read by the phone, the phone leaves, then the switch to the full
   firmware; with the server stopped the phone reads 0x19 and nothing is stored.
6. The full firmware's first check-in after each switch reaches the server (it is online in GET /v1/devices).

## Pass

Only a stored network that reaches the API hands over; every failure keeps the setup firmware awake and reachable,
named by its step; a BLE setup hands over after the phone read SAVED.

## Result

- 2026-11-02 06:26-06:28 UTC, the pair (full 0.6+g1d2b375-dirty-202611020620 in slot 0, setup_test
  0.6+g1d2b375-dirty-202611020621 in slot 1), USB-C 1 in, `switch_to_full usb` (the plug's code without the hand):
  - server up: the stored network passed its check up to the API, "slot: 1 -> 0"; the console dropped 3.1 s after the
    command, the full firmware answered at 9.6 s.
  - server stopped (`sudo systemctl stop leakcam-server` on the Pi, started again right after): "the stored network
    failed its check (state 0x19 detail 0): setup stays" after 1.4 s; still the setup firmware, also once the server
    was back (no retry of its own: the next plug or the 6 h retry, test125).
- 2026-11-02 06:35 UTC, **step 1 PASS, a real plug from BLE sleep** (`switch_to_full on`, the runner started the window
  on the unplug): unplug seen 06:35:13, sleep window 06:35:15 (console gone at once: asleep, advertising), USB-C 1
  plugged by the user, the console back at 06:35:40 as **the full firmware** (slot 0, table age 12): wake, the stored
  network's check up to the API and the switch within 25 s of the window's start (the plug's own moment is by hand,
  not timed).
- 2026-11-02 06:36-06:38 UTC, the rest (the bench phone, BleOp):
  - step 5, BLE setup: paired with the PIN (11.8 s, the phone's dialogs), commit 01: joining 0x01 -> address 0x02 ->
    internet + API 0x03 -> SAVED 0x04 in 2.1 s; the phone left, the console went 1.2 s after the session, the full
    firmware answered 21.0 s after the session's start.
  - step 6: the full firmware online at the server right after (GET /v1/devices/{id}: online).
  - step 3, a wrong passphrase stored unchecked (commit 02, hand-over off): `switch_to_full usb` -> "failed its check
    (state 0x11 detail 33)" after 5.0 s, setup stays.
  - step 4, `pt_ble erase`: "no network stored: setup stays".
  - the real network stored again afterwards (commit 02), its length + CRC equal to ~/.leakcam_wifi's.
