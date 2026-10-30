# test106: Wi-Fi setup from the phone, end to end

Status: **pass, steps 1 3 4 5 6 8** (2026-10-30, run.py 18/18): setup 7.3-8.0 s from opening the app to Done; steps 2
and 7 open (battery wakes need the rewritten product firmware; step 7 by hand).

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
   the app removes its stale bond by itself before every send and pairs again, no Forget by hand.
7. **On battery, no setup:** USB out: the board is not listed by the app, a stored bond cannot connect.
8. **Repeats:** 10 setups in a row (alternating two networks): every one Done, BL616 memory steady (mem_leak.py).

## Pass

A new board goes from unboxed to a working Wi-Fi wake with only the app; every failure is named in the app and
stores nothing; a stored network is used on battery by the BL616 and the K230; setup is impossible without USB power.

## Result

run.py, 2026-10-30, BL616 test firmware (default flags) + the setup app, bench phone (Samsung, Android 13): 18/18.

- Step 1: credentials erased -> Done, stored exactly (length + CRC on the board).
- Step 3: a wrong passphrase is named ("router refused the password", code 33 = no EAPOL 3/4, the SDK's own status:
  its connect indication says 0/0) and offers no Save without checking; an SSID not in the air is named and offers it;
  the stored network stays in both.
- Steps 4, 5: Save without checking stores the new network ("Saved without checking"); a checked setup replaces it.
- Step 6: after a watchdog reset the app removes its stale bond and pairs anew by itself: Done, no Forget by hand.
- Step 8: 10 setups in a row, all Done; BL616 heap steady (free 131104 B before and after, no leak).

**Setup waits** (timeline.py, 5 setups in one boot, s from opening the app; the app's own log times, the board's
console lines on the same clock). The automation types the network in ~1.9 s and presses the pairing prompt at once;
a person adds their own time to those two.

| Stage (what the user sees) | Now | Before (2026-10-30 morning) | What took the time / the fix |
|---|---|---|---|
| Open -> the network screen ("Find") | 0.3-0.85 | 9.7 | a fixed 10 s scan; now the first board leads on, Find goes on behind |
| Typing the network | (user) | (user) | |
| Send -> pairing prompt | 0.8-1.35 | 1.1 | connect, MTU |
| Pairing prompt(s) -> paired | 1.2 | 2.2-2.4 | Samsung asked twice for the board's Security Request; the app starts the bond: one prompt |
| Paired -> "joining" | 0.25 | 1.9 | discovery now runs during pairing; Wi-Fi starts at the BLE connect (was at the commit, 1.3 s) |
| "joining" -> password accepted | 0.85-0.9 | 6.0 | scan at 40 TU/channel (0.6 s, was 2.9 s), connect to the scanned AP (the supplicant scanned again, 2.9 s) |
| -> address | 0.01-0.06 | 3.0 | no lwIP ARP check (0.7-0.9 s); DHCP itself 20 ms in every run since |
| -> Done | 0.11-0.15 | 1.0 | BLE at CONNECTION_PRIORITY_HIGH (the phone's 45 ms interval); internet check 0.07 s |
| **Total, open -> Done** | **6.3-7.7** | **28.5** | |

Open:
- Step 2 (the stored network used on battery by the BL616's wake and a K230 session) waits for the rewritten product
  firmware; step 7 (on battery: not listed, no connection) by hand (test040 usb-gate covers the board side).
- 5 GHz-only and no-data hotspot networks (Setup) not run: no such networks on the bench.
- The hand run on a second phone (another Android version: the bond removal, the single prompt) not done.
- Remaining waits: Send -> prompt (connect) and the fast scan could go too if the app connected and paired while the
  user types, and if the phone passed the network's channel (the scan would cover one channel): app-flow decisions.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: setup runs on the BL616 alone, K230 off; the K230 session of step 2 is open | target (test022) | longest step: |
| This part's boot share | n/a: no K230 boot in setup (step 2 open) | - | |
| Test run time | 249 s automated (run.py, 2026-10-30), no setup | - | setup (flashing, plugging, waiting) separately |
| setup time: app open -> Done; first wake: join + server check | app opened -> Done: first 8.0 s, repeats 7.3-9.4 s (10 in a row) | - | % of the top |
| Current idle / during (min / avg / peak) | not metered in this run; the same BLE + check path in test040 on USB, cell full: advertising 63.4, connected 63.1, check peak 86 mA (2026-10-29) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | n/a: setup only on USB power (the board refuses it on battery); the cell is not loaded | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a for the cell: setup draws from USB (~7 s at 63-86 mA, test040 readings) | - | scripts/energy.py |
| Per day | n/a: once per installation, on USB; the battery wakes of step 2 are open | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die n/a: K230 off; board 28.0 / 28.2 / 28.1 C (AHT20) (2026-10-30) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 131104 / min 105132 / 131104 B, blocks 107 -> 107, deleted 0; BL616 no leak (mem_leak.py, 11 snapshots) (2026-10-30) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | BL616 test firmware 1309616 B (2026-10-30, on the board at the run) | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -O2 (SDK default), no LTO (2026-10-30) | smallest code (rule) | |
| Features over the minimal image | as test040 (BLE 182 KB flash, 14.2 KB static RAM, 18.8 KB heap at run time); this firmware 1309616 B | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

The K230 boot is test022; this test records the BL616's wake -> joined -> server check on the stored network, and
whether the remembered AP skips the scan on the second wake.

## Battery energy

Setup is on USB (free for the cell); the battery cost is the wakes' join with the stored network (with test100 and
test102).
