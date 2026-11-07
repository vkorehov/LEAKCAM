# test122: battery low - the K230 stops at once, the BL616 tells the user to charge

Status: **planned** (2026-11-01, user: "use divider only at k230 to save power. And k230 needs to shutdown quickly in
case battery is depleted, to allow bl616 to communicate over wifi or ble that it has nearly discharged battery, please
charge"). Design: DESIGN.md 5.3.1 (battery low).

The battery divider is only on the K230 side (R59/R61 through Q1, on the K230's rail): no sleep current, and the BL616
needs no reading of its own (bug-004, accepted: the cell's protection board at ~2.4 V is the last guard). So the K230
measures in every session; below the cutoff it stops at once, and the BL616 keeps the battery-low state (flag + last
VBAT) in HBN RAM and tells the user on every channel (user, 2026-11-01): Wi-Fi (one check-in with the flag: the server
keeps it, the app shows "battery low, please charge"), BLE (the flag in the advertisement and in STATUS), USB (the
console status and the USB setup path's status); then the lockout (only the USB plug wakes it, test038).

## Setup

- Full firmware (firmware/bl616_full) and the K230 product image; the presence server (server/) and the app.
- **Without a lab supply** (none on the bench): the cutoff raised above the cell's voltage, as test038 did (`bat_watch
  <cut_mv>` on the K230 side: the K230 holds the cutoff level, the BL616 has no reading), so the real cell trips it; the real 3.10 V with a
  lab supply on the next assembled board (test038's planned part).
- The phone on adb with the app; the board on the cell, USB-C 1 out (the plug ends the lockout, the last step).

## Needs (build features and dependencies)

K230: ADC_1 VBAT in `/app/leakcam`, the `;b=<mV>` attribute on every K230 frame (DESIGN.md 5.3.9), the quick stop
(session end on a low reading). BL616: the
battery-low state and the lockout (aon_state in HBN RAM; the level is the K230's), the battery-low flag in the check-in (presence.c, wifi_check.c) and in the BLE
advertisement (ble_pairing.c). Server: the flag kept per device and shown in GET /v1/devices/{id}. App: the
"battery low, please charge" state.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

0. Low level, the state: a cut stores the flag and the last VBAT in HBN RAM (aon_state); it survives a PDS/HBN sleep, a
   software reset and a watchdog reset (read back each time), and clears only at recovery (step 5).
1. Low level, the K230's quick stop: a session running (capture or upload); the reading below the cutoff -> the product
   program ends the session (no new capture or upload, a write in flight finished: no NAND damage, test028); time from the
   low reading to the K230's rails off; the current before and after.
2. The BL616's notice over Wi-Fi: right after the rails go off, one check-in with the battery-low flag and the VBAT
   reading; the server shows it (GET /v1/devices/{id}); the app's list shows the board as "battery low, please
   charge" (UI Automator reads the row); the time from the cut to the server having it, and its energy.
3. The notice over BLE: the advertisement carries the flag while the board advertises (setup mode, or after presence is
   lost): BleOp scan reads it; the app shows it for a board it sees only over BLE (no network stored).
3a. The notice over USB: with USB-C 1 plugged in during the lockout (before the cell is back above 3.50 V) the console
   status (`provision`) and the USB setup path's status (test113) show battery low and the last VBAT.
4. Lockout: no K230 session for an RTC wake or a leak on S1 (1 h); current = the sleep floor; the notice is not sent
   again every wake (one per cut).
5. Recovery: USB plugged -> charging; the lockout ends with the next reading >= 3.50 V; the flag clears at the server
   and in the app at the next check-in.
6. Without Wi-Fi (network gone): the notice falls back to the BLE flag alone; the server marks the board offline as
   before, and the app shows the last known battery state.
7. **The reading is the shared one (2026-11-06, DESIGN.md 5.3.9):** the K230's `;b=` attribute on every frame, kept by
   battery.c in HBN RAM; the low flag uses the same value, and the app's battery icon shows it red at <= 15 %.

## Pass

Every cut stops the K230 within a second of the low reading, without NAND damage; the user sees "battery low, please
charge" in the app (through the server, or over BLE without a network) before the board goes quiet, and on USB when
plugged; the state survives every sleep and reset in HBN RAM; the lockout holds
until USB is plugged and the cell is back above 3.50 V; the flag then clears.

## Result

Not run.
