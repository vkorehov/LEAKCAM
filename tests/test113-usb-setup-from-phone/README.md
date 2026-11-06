# test113: full setup from the phone app over a USB-C cable (high level, product firmware)

Status: **board side pass, app side open** (2026-11-05). Full provisioning over the cable on the product firmware
works with this PC in the phone's role (below); the app's USB host code is not written yet. Low level done: the K230's
USB device against the bench (test043), the phone sees it as host, 1.41 s after the K230 starts (test044), link frames
reach the K230 program (test045).

The whole provisioning of a LEAKCAM with no network stored, by the LEAKCAM Setup app on the bench phone over a USB-C
cable into USB-C 1, BLE not used: on the **product firmware** pair (BL616 setup + full) and the **product K230 image**
(firmware/k230_board, not the test image).

## How it works (DESIGN.md 5.3.4, 5.3.8)

  phone app (USB host) --USB-C 1--> K230 USB0, CDC ACM --link UART--> BL616 setup firmware: provisioning

- One K230 product image; the BL616 picks what it runs at the start: the K230 program asks READY, the BL616's WAKE
  answer names the reason, `setup` = the USB setup relay (no camera, no Wi-Fi on the K230). Any other reason: the
  normal session (later use cases).
- The setup firmware powers the K230 for this when USB-C 1 gets VBUS with no network stored, and off again when the
  cable is pulled or the network is saved.
- The provisioning is the BLE one (ble_pairing.c's operations without the GATT): the same PIN from the server, SSID,
  passphrase, commit with an attempt id, STATUS (joining, address, internet, saved). Credentials never on a console.

## Steps

1. Hand: USB-C 1 from the bench to the phone; the board has no network stored (setup firmware, asleep).
2. The plug: the setup firmware wakes on VBUS, powers the K230, sends WAKE setup; the phone lists the board's USB
   device; time from the plug.
3. The app: the board's row shows the USB link icon (not the plug); its device ID read over the cable.
4. The app gets the PIN from the server (registers the phone) and proves it over the cable; a wrong PIN is refused.
5. Wi-Fi setup over USB: SSID, passphrase, commit; STATUS joining -> address -> internet -> saved; the app shows Done.
6. The board: K230 off, the switch to the full firmware, it checks in at the server; the app lists it online.
7. Failures as over BLE: a wrong password named, a network not seen named, the old state kept.
8. The cable pulled during the check: plugged back, STATUS read for the attempt (test110's rule).
9. Hand: USB-C 1 back to the bench.

## Needs (build features and dependencies)

- K230 product image: CherryUSB device + CDC ACM in the product kernel config; the product program (started at boot)
  with the start mode from WAKE and the USB setup relay.
- BL616 setup firmware: the K230 power (k230_power.c, now full firmware only), the link UART, WAKE setup, the
  provisioning split from BLE into a transport-free part used by BLE and the link.
- The app: USB host (UsbManager, the one permission dialog, remembered), the USB channel first (USB > BLE > Wi-Fi),
  the USB link icon.
- Hands: the cable bench <-> phone (adb over Wi-Fi meanwhile).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Pass

A LEAKCAM with no network is fully set up from the app over the cable, on the product firmware, as reliably as over
BLE, and only by an app holding the PIN.

## Result

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

Open: the app (UsbManager, permission, USB > BLE > Wi-Fi channel order, the USB link icon), then this test's run.py
with the phone.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | **MISSING** | target (test022) | the K230 boots for the USB session |
| This part's boot share | **MISSING** | - | |
| Test run time | **MISSING** | - | |
| setup over USB: plug -> Done | **MISSING** | - | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 meter |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a for the cell: on USB | - | |
| Per day | n/a: once per installation | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak | |
| Image (sizes) | **MISSING** | minimal image (rule) | |
| Compile flags | **MISSING** | smallest code (rule) | |
| Features over the minimal image | K230 USB device CDC ACM, the relay | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

The K230 boot is part of a USB setup (the BL616 starts it on VBUS): measured with step 1.

## Battery energy

n/a: on USB power.
