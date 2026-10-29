# test106: Wi-Fi setup from the phone, end to end

Status: **planned** (2026-10-29): not run yet; step 6 rewritten 2026-10-29 (the board keeps no BLE pairings).

The installation flow as a user meets it: a new LEAKCAM on USB power, the LEAKCAM Setup app (android/, APP.txt) finds
it, pairs, sends the Wi-Fi network, the board checks and stores it in the BL616's flash; then, on battery, the
product's wakes use it (BL616 server check, K230 session over SDIO Wi-Fi). The BL616 side alone, characteristic by
characteristic, is test040.

## Setup

- Product BL616 firmware with BLE provisioning, or the test firmware of test040 until the product firmware is
  rewritten (firmware/bl616 hangs before USB on rev 1); K230 test image (test019) for the session step.
- Android phone with the LEAKCAM Setup app (debug APK, `./gradlew assembleDebug`), on adb (USB to the VM or Wi-Fi
  adb); the app's steps driven by `adb shell uiautomator` / UI Automator so the run needs no hands, the system
  pairing dialog included. One run by hand on a second phone (another Android version) for the dialogs.
- Networks: the test network from `~/.leakcam_wifi`, a wrong passphrase, a 5 GHz-only SSID, a hotspot without data,
  a second 2.4 GHz network for the change.
- USB-C 1 in for the setup; cell connected for the battery steps. Probes: USB-C 1 VBUS meter, VBAT at C15.1.

## Needs (build features and dependencies)

Everything of test040 (BLE, easyflash, the Wi-Fi check), plus the product wake path: RTC / leak wake, BL616 Wi-Fi join
from the stored credentials and the remembered AP (`wifi_link.c`), the K230 SDIO Wi-Fi bridge (test100). Phone:
Android 8.0+, the app's BLE permissions.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. **First setup:** credentials erased, USB in; app: Find LEAKCAM -> the board listed -> enter the network -> pairing
   dialog -> check -> "Done". Time from opening the app to Done; the app's status steps match the board's.
2. **On battery:** USB out; the next wake joins the stored network (remembered AP filled on the first join) and
   reaches the server (BL616 server check, test100 step 4), then a K230 session gets an address over SDIO Wi-Fi.
3. **Failures shown, nothing stored:** wrong passphrase, 5 GHz-only SSID, hotspot without data: the app's message
   names the reason; the old network still stored and still joined on the next wake.
4. **Save without checking:** away from the network: stored, the app says it was not checked.
5. **Change network:** a second setup replaces the first; the next wake joins the new one.
6. **Board reset (bond lost):** the board keeps no pairings (user, 2026-10-29), so any reset or sleep loses the bond:
   the app removes its stale bond by itself and pairs again, no Forget by hand (the app still asks for it today:
   change it first, as BleOp's unpair).
7. **On battery, no setup:** USB out: the board is not listed by the app, a stored bond cannot connect.
8. **Repeats:** 10 setups in a row (alternating two networks): every one Done, BL616 memory steady (mem_leak.py).

## Pass

A new board goes from unboxed to a working Wi-Fi wake with only the app; every failure is named in the app and
stores nothing; a stored network is used on battery by the BL616 and the K230; setup is impossible without USB power.

## Result

Not run yet.

Open:
- The app still asks the user to Forget the stale bond by hand; it must remove it by itself (step 6) before the run.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | **MISSING**: the K230 session of step 2 | target (test022) | longest step: |
| This part's boot share | **MISSING** | - | |
| Test run time | **MISSING** | - | setup (flashing, plugging, waiting) separately |
| setup time: app open -> Done; first wake: join + server check | **MISSING** | - | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING** | - | scripts/energy.py |
| Per day | **MISSING**: the wakes' join on battery (setup itself: once, on USB) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | **MISSING** | minimal image (rule) | the image this run used |
| Compile flags | **MISSING** | smallest code (rule) | |
| Features over the minimal image | **MISSING**: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | **MISSING**: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

The K230 boot is test022; this test records the BL616's wake -> joined -> server check on the stored network, and
whether the remembered AP skips the scan on the second wake.

## Battery energy

Setup is on USB (free for the cell); the battery cost is the wakes' join with the stored network (with test100 and
test102).
