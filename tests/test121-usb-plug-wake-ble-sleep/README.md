# test121: USB plugged in wakes the board from BLE sleep

Status: **pass** (2026-11-01, 2/2 plugs; user: "we can detect usb plugged in? if usb plugged in we switch to 'full' firmware
where we enable all"; "separate low level test better").

The low-level proof that the setup firmware notices USB-C 1 being plugged in while it sleeps in PDS with BLE
advertising (test120), so it can hand over to the full firmware (DESIGN.md 5.3.1: slot switch and reset; the full
firmware brings up the K230 and the USB setup path, test113). Only the detection here: no slot switch, no K230.

USB-C 1's VBUS feeds the BQ24072, whose PGOOD (open drain, 100 k to 3V3_SLEEP) goes low when USB power is good; it is
the BL616's IO3 (board_pins.h PIN_USB_PGOOD). The SDK's low-power IO wake (`bl_lp_io_wakeup_cfg`) unmasks only IO3, on
its falling edge (group IO0-7), no pulls added in sleep.

## Setup

- Setup firmware tests/firmware/bl616_lp (`lp_boot`, then the product BLE advertising every 2 s by itself).
- The bench console on PR1 (the BL616's own USB, /dev/leakcam-bl616), which drops while the chip sleeps.
- The cell in and charged; **USB-C 1 unplugged** at the start (the meter/charger cable): one unplug and one plug by
  hand per run.

## Needs (build features and dependencies)

BL616: CONFIG_LPAPP + lpfw, tickless sleep (test120), the PDS IO wake (bl_lp_io_wakeup_cfg on IO3), BLE (ble_pairing.c).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. USB-C 1 out: `lp_sleepstat` says USB power no.
2. `wakeup_timer 120000 0`: the console drops (asleep, advertising).
3. By hand, ~30 s in: USB-C 1 plugged in.
4. Expected: the console back within a few seconds (before the window's end), "lp: USB plugged (PGOOD low, wake 1)";
   `lp_sleepstat` USB plug wakes 1, USB power yes.
5. With USB in: `wakeup_timer 30000 0` refuses ("USB power present: no sleep window, the full firmware runs").
6. Unplug: a new window sleeps again (BLE advertising, phone scan finds it).

## Pass

Every plug during BLE sleep wakes the board within seconds and is reported as a USB plug; with USB present it does not
sleep; after unplugging it sleeps again.

## Result

- 2026-11-01, setup test firmware (tests/firmware/bl616_lp, product BLE advertising every 2 s), USB-C 1 plugged and
  unplugged by the user:
  - plug 1: unplug seen 20:17:37 UTC, window started 20:17:38, console back 20:18:13 (85 s before the window's end),
    `lp_sleepstat` USB plug wakes 1, USB power yes; 18 sleeps while waiting.
  - with USB in: `wakeup_timer 30000 0` refused ("USB power present: no sleep window"), the console stayed.
  - plug 2: unplug seen 20:19:00 (asleep again: 16 more sleeps), console back 20:19:30 (92 s early), USB plug wakes 2.
  - the "lp: USB plugged" line is printed before USB has re-attached and is lost on the bench; the counter shows it.
- Not measured: the time from the plug to the wake (the plug moment is by hand; the console's return includes ~4 s of
  VMware attach). To add: the wake's own timestamp against the RTC.
