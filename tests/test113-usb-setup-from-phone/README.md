# test113: Wi-Fi setup from the phone over USB (like BLE, over the cable)

Status: **planned** (2026-11-01, user: setup over the phone's USB connection, as over BLE).

The LEAKCAM Setup app configures the board over a USB-C cable from the phone (Android as USB host, OTG): the same
operations and the same reconciled state as over BLE (SSID, passphrase, commit with attempt id, STATUS), so the app's
send code stays one with two transports.

## The hardware path (rev 1)

USB-C 1 (the enclosure's only USB opening) is the K230's USB0 in device mode, not the BL616 (DESIGN.md: USB): the
BL616's own USB is PR1, the debug pads inside the enclosure. So over USB the phone talks to the K230:

  phone (USB host, OTG) --USB-C 1--> K230 USB0 (device: CDC ACM) --link UART1 (link_mux)--> BL616 provisioning

- K230: a USB device CDC ACM function on USB0 in RT-Smart (removed from the minimal image, test008: to bring back);
  the K230 must be powered while the phone is plugged in: the BL616 sees VBUS (usb_present) and starts a K230 session
  for it.
- A small K230 relay: CDC frames <-> link frames to the BL616, which runs the same provisioning as for BLE (the
  ble_pairing.c GATT handlers split into a transport-independent part).
- Security: the cable is physical access, like the USB power gate; the same pairing PIN (from the server, entered by
  the app) authenticates the session, so a phone without the app cannot configure it over USB either.
- Rev 2 option (bugs/v2.txt): route the BL616's USB to USB-C 1 instead (a mux or the second connector): the BL616 then
  answers alone, no K230 boot.

## Steps (when built)

1. Low level: the K230 enumerates as CDC ACM on the phone (OTG), on the Mac; frames echo through the link to the
   BL616 and back.
2. The app sends a network over USB: Done, stored, same STATUS states and times as BLE.
3. Loss: the cable pulled during the check: the app reconnects (BLE or USB) and reads STATUS (test110's rule).
4. No PIN: refused.

## Needs (build features and dependencies)

K230 USB device stack (CDC ACM) and a relay program; the BL616 provisioning split from BLE; the app's USB host code
(UsbManager, permission dialog); a USB-C OTG cable phone <-> LEAKCAM, adb over Wi-Fi meanwhile (the phone's own USB
is in use). Hands: the cable change between the Mac and the phone.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Pass

A LEAKCAM is set up from the app over the cable as reliably as over BLE, and only by the app.

## Result

Not run (planned).

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
