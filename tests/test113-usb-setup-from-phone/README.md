# test113: full setup from the phone app over a USB-C cable (high level, product firmware)

Status: **pass** (2026-11-07 07:48-07:51, BL616 pair 0.6+g5fc1138-dirty (the code of 58bf10c), K230 0.6+g98e16d7: 18/18, the cable screen
5.18 s after the plug; step 12, Change Wi-Fi on the set-up board over the cable: SETUP -> the setup firmware's relay
back 2.6 s, Done 1.2 s later, the full firmware's relay back 4.6 s after Done with the Done screen kept). The phone's plug opens LEAKCAM Setup on the cable screen of the board (4.3 s), the
app sets the network up over the cable (no Bluetooth) to Done, the board saves it, goes back to the full firmware and is
online at the server; product BL616 pair + product K230 image. Low level: test043 (K230 USB device), test044 (the
phone as host), test045 (link frames reach the K230 program).

The whole provisioning of a LEAKCAM with no network stored, by the LEAKCAM Setup app on the bench phone over a USB-C
cable into USB-C 1, BLE not used: on the **product firmware** pair (BL616 setup + full) and the **product K230 image**
(firmware/k230_board, not the test image).

## How it works (DESIGN.md 5.3.4, 5.3.8)

  phone app (USB host) --USB-C 1--> K230 USB0, CDC ACM --link UART--> BL616 setup firmware: provisioning

- One K230 product program, the same on every power-up (DESIGN.md 5.3.8): READY, the WAKE (the reason only logged),
  then the USB relay and the leak job in parallel; the BL616 gives the cable's lines their meaning.
- On USB power both BL616 firmwares keep the K230 on (DESIGN.md 5.3.11): the setup firmware's relay carries the
  provisioning; after SAVED the K230 goes off with the switch to the full firmware, whose session brings the relay
  back (ID ... full: step 12's Change Wi-Fi). A pull switches it off.
- The provisioning is the BLE one (ble_pairing.c's operations without the GATT): the same PIN from the server, SSID,
  passphrase, commit with an attempt id, STATUS (joining, address, internet, saved). Credentials never on a console.

## Steps

1. Hand: USB-C 1 from the bench to the phone; the board has no network stored (setup firmware, asleep).
2. The plug: the setup firmware wakes on VBUS, powers the K230, sends WAKE setup; the phone lists the board's USB
   device; time from the plug.
3. The app: the board's row shows the USB link icon (not the plug); its device ID read over the cable.
4. The app gets the PIN from the server (registers the phone) and proves it over the cable; a wrong PIN is refused.
5. Wi-Fi setup over USB: SSID, passphrase, commit; STATUS joining -> address -> internet -> saved; the app shows Done.
6. The board: the switch to the full firmware (the K230 off and on again with it), it checks in at the server; the
   app lists it online.
7. Failures as over BLE: a wrong password named, a network not seen named, the old state kept.
8. The cable pulled during the check: plugged back, STATUS read for the attempt (test110's rule).
9. Hand: USB-C 1 back to the bench.
10. **Battery over the cable (DESIGN.md 5.3.9, test131):** the `ID` answer carries the battery and USB power
    (`ID <id> <stored> <mV> 1 <setup|full>`); the cable screen's row shows the battery icon green, filled to the board's `battery`.
11. **The setup in the metrics (DESIGN.md 5.3.10, tests/test048):** the credential check of step 5 is itself a
    check-in (the setup firmware's, the K230 still relaying): `chg=1`, `usbw=1` (the plug woke the setup firmware),
    `ks` = the relays that ended before it (the bench cable's when it was pulled: 1), not the running one; it starts the
    counters again. The full firmware's first check-in after step 6: `ks=1`, `klast=1`, `kms` ~
    the relay's rails-on time, `kver` (the K230 program that relayed), `sw=1`, `usbw=0`. The wrong password of step 7
    never reaches the server (no sample, `fails` +1 in the next one); `t`/`rh`/`probe` in every sample.
12. **Change Wi-Fi on a set-up board over the cable (DESIGN.md 5.3.4):** the board on the full firmware, the phone in
    USB-C 1: the app opens on the cable screen (`creds 1`); one Send: the app reads `ID ... full`, sends `PIN`,
    `SETUP`, rides out the switch (the relay gone and back with the setup firmware, `ID ... setup`), then `PIN`,
    `SSID`, `PSK`, `COMMIT`; Done; the board back on the full firmware, online; when its relay comes back on the cable
    the Done screen stays. Phone (2026-11-07): the link back on the setup firmware 2.6 s after it went, Done 1.2 s later.
    Bench run (the PC as the phone, tools/test-runner/cable.py): the relay gone 0.9 s after SETUP's OK, back 6.6 s
    (the PC's enumeration + udev), SAVED 1.2 s later, 8.0 s in all.

## Needs (build features and dependencies)

- K230 product image: CherryUSB device + CDC ACM in the product kernel config; the product program (started at boot)
  with the USB relay and the leak job (the same on every power-up).
- BL616 setup firmware: the K230 power on USB (k230_session.c, shared), the link UART, the cable commands
  (usb_setup.c), the provisioning split from BLE into a transport-free part used by BLE and the link. Full firmware:
  the cable's ID/PIN/SETUP (usb_full.c).
- The app: USB host (UsbManager, the one permission dialog, remembered), the USB channel first (USB > BLE > Wi-Fi),
  the USB link icon.
- Hands: the cable bench <-> phone (adb over Wi-Fi meanwhile).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Pass

A LEAKCAM with no network is fully set up from the app over the cable, on the product firmware, as reliably as over
BLE, and only by an app holding the PIN.

## Result

2026-11-06, run.py with the bench phone (65 s, 8/8 checks), board with no network stored:

- The phone in USB-C 1 (after the bench cable): the setup firmware wakes from sleep on VBUS, powers the K230 (3V3 in
  1.6 ms), READY 0.82 s after the run, WAKE setup; the app opens by itself (default handler, "always" ticked once for
  the name LeakCam) on the cable screen of 7C3E8262B149, no network: **4.26 s after the plug**.
- The app's setup over the cable, 3 screens (enter the network, joining, Done), no pairing, no list, no Find: the
  driver's whole op 12.7 s; network stored, full firmware, server online at once.
- Fixed on the way (HISTORY Obstacles): K230_PWR dead after the setup firmware's sleep (BL616 fix in k230_power.c), a
  `k230 off` during a session recorded as a crash, the test driver scrolling for a missing Find button.

2026-11-05, bench run, this PC in the phone's role (scratch script on `/dev/leakcam-k230usb`; PIN from the server,
network from ~/.leakcam_wifi, nothing printed), product BL616 pair + product K230 image, no network stored:

- The setup firmware powered the K230 by itself on USB power (no network stored); the program's READY 775 ms after
  the run, WAKE setup 1 ms later; the relay on `/dev/ttyGS0` at 591 ms of the K230's boot.
- `ID` -> `ID 7C3E8262B149 0` in 2 ms; before PIN every command `ERR AUTH`; a wrong PIN `ERR PIN` (1 of 5).
- `PIN` `OK`, `SSID` `OK`, `PSK` `OK`, `COMMIT 1 7`: STATUS joining -> address 0.9 s -> internet 3.9 s -> **SAVED
  4.9 s** after the commit; answers 1-3 ms each way (PC -> K230 USB -> link -> BL616 -> back).
- 2.3 s after SAVED: `k230: off (network saved)`, 160 heartbeat edges, rx dropped 0, fifo overflows 0, tx failed 0,
  3V3 fall 10 ms; then slot 1 -> 0, the full firmware checks in: server `online: true`. No crash record.
- Order: the pushed `STATUS 1` came **before** COMMIT's `OK` (the status listener runs inside the commit, before the
  answer). The app takes STATUS lines by their keyword, never as the reply to the command it just sent.

2026-11-06 09:15, run.py with steps 7-8 (148 s, 13/13 checks, one plug by hand):

- Step 7, a wrong password over the cable: "The router refused the password ... (code 33)", nothing stored, the board
  stays in setup with the cable screen for the retry.
- Step 8, the cable pulled during the join check (the phone's port switched to sink/device for 3 s: VBUS and the link
  gone, no hand): the app shows "unplugged before it confirmed the network ... cannot tell whether it was stored",
  never Done, and goes to the board list; the board finishes the check by itself: network stored, full firmware,
  online 3 s later.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a in this run: the K230 boot is timed by test045 (READY 0.43 s after pt_run) | target (test022) | the K230 boots for the USB session |
| This part's boot share | n/a: the setup session's K230 boot, see test045 | - | |
| Test run time | 148 s automated (run.py, 2026-11-06, steps 7-8 in), plug -> end about 70 s | - | |
| setup over USB: plug -> Done | plug -> cable screen 4.26 s; the app's setup op 12.7 s over the cable (2026-11-06) | - | % of the top |
| Current idle / during (min / avg / peak) | n/a: on the phone's USB power, not metered | floor (test031) | USB-C 1 meter |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a for the cell: on USB | - | |
| Per day | n/a: once per installation | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | BL616 die 37-39 C, board (AHT20) 38-42 C in the check-ins (it reads the warm board, bugs/v2.txt 12) | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 209772 / min 190728 / 209772 B, blocks 62 -> 62, deleted 0; leak: n/a, 1 snapshots (mem_leak.py needs >= 8 cycles) (2026-11-06) | no leak | |
| Image (sizes) | BL616 test firmware 1322096 B (2026-11-06, on the board at the run) | minimal image (rule) | |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -Os -g (SDK Kconfig default), no LTO (2026-11-06) | smallest code (rule) | |
| Features over the minimal image | K230 USB device CDC ACM, the relay | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

The K230 boot is part of a USB setup (the BL616 starts it on VBUS): measured with step 1.

## Battery energy

n/a: on USB power.
