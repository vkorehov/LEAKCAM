# test110: setup app recovery from loss at every step

Status: **pass, 37/37** (2026-11-01 run 3, run.py on the BL616 test firmware): lost notifications, a link lost at any
step after the save, the phone's Bluetooth off during the check: the app reconnects, reads the board's STATUS for its
attempt and shows the board's result (Done 7.1-14.6 s); a link lost on every connect, Bluetooth off before the save,
DNS / HTTP loss: named in 5.7-11.1 s; app and board agree in every case.

Every interaction of a Wi-Fi setup from the phone (test106) with a loss injected into it: the app must either recover
by itself or tell the user a named failure in time, its message must match what the board stored (eventual
consistency: STATUS is the board's state and the app reconciles with it), and a clean setup right after must work.

## Setup

- BL616 test firmware (tests/firmware/bl616_powertest) with `pt_fault`: linker wraps around the product's
  provisioning (firmware/bl616/ble_pairing.c, wifi_check.c), the product sources unchanged. BLE retransmits on the
  link layer, so a loss there is a dropped notification (host buffers full) or a dropped link (supervision timeout);
  on Wi-Fi a DNS or HTTP answer that never comes.
- Android phone with the LEAKCAM Setup app on adb (test106 Setup), unlocked, Bluetooth on; USB-C 1 in.
- The test network from `~/.leakcam_wifi`; a decoy network ("t110-decoy") stored directly by the test firmware (`pt_ble decoy`, under a second; through the
  app it took 16 s a case) before each case, so the
  stored network after the case says whether the board stored the real one.

## Needs (build features and dependencies)

Everything of test106 (BLE provisioning, easyflash, the Wi-Fi check) plus `pt_fault` (wraps bt_gatt_notify_cb,
bt_conn_set_security, lwip_getaddrinfo, lwip_recv). Phone: adb `cmd bluetooth_manager` for the phone-side loss.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. **Baseline:** a setup without faults, Done.
2. **Lost notifications** (`notify all`, `notify half`, `notify final`): the app reads STATUS too, so Done.
3. **Link lost** right after connect and when STATUS reaches joining, address, internet and the result: a named
   failure within 35 s (not the 60 s step timeout); the app's claim matches the board (Done only if stored, "Nothing
   was stored" never with the network stored).
4. **Wi-Fi answers lost** (`dns`, `http`): the reason named, the decoy kept.
5. **Phone Bluetooth off** 6.5 s into the setup for 4 s: a named failure, app and board agree.
6. After every case that did not end in Done: the fault off, a clean setup Done (recovery; a Done case was itself a
   full setup); BL616 memory sampled at the end.

## Pass

No case leaves the user waiting for a timeout when the board knows the answer, no case ends with the app and the
board disagreeing, and every case is followed by a working setup.

## Result

run.py, 2026-11-01 08:25 (run 3), BL616 test firmware with pt_fault + the setup app with reconciliation, bench phone
(Samsung, Android 13): 37/37, 647 s; crash record none before and after; BL616 free 122796 B at the end.

| Case | App, time | Board |
|---|---|---|
| every notification lost (5) / every second (2) / the result's (1) | Done 7.3 / 7.1 / 7.4 s | real network |
| link lost at every connect (6 faults = 3 connects) | "disconnected (133)", 5.7 s | decoy |
| link lost when the check starts / getting an address / reaching the internet / with the result | Done 9.5 / 10.0 / 10.2 / 10.3 s (reconnect + STATUS read) | real network |
| DNS / HTTP answer lost | named, 7.2 / 7.3 s | decoy |
| phone Bluetooth off 4 s before the save | "Bluetooth is off", 11.1 s | decoy |
| phone Bluetooth off 4 s during the check | Done 14.6 s (reconnected when it came back) | real network |

Every clean setup after a case: Done, 7.1-7.6 s. The fixes that got here (run 1: 31/34) are in HISTORY.md.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: none (on USB; the setup's current is test040's).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: setup runs on the BL616 alone, K230 off | target (test022) | longest step: |
| This part's boot share | n/a: no K230 boot in setup | - | |
| Test run time | 605 s automated (run.py, 2026-11-01), no setup | - | setup (flashing, plugging, waiting) separately |
| recovery: per case app message, time, board state | 2026-11-01 run 3: notifications lost -> Done 7.1-7.4 s; link lost after the save (4 points) -> Done 9.5-10.3 s; Bluetooth off during the check -> Done 14.6 s; link lost on every connect 5.7 s, Bluetooth off before the save 11.1 s, DNS 7.2 s, HTTP 7.3 s -> named, decoy kept; app and board agree in all 12 | - | % of the top |
| Current idle / during (min / avg / peak) | n/a: not metered; the same BLE + check path in test040 on USB: advertising 63.4, connected 63.1, check peak 86 mA (2026-10-29) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | n/a: setup only on USB power | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a for the cell: setup draws from USB | - | scripts/energy.py |
| Per day | n/a: once per installation, on USB | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die n/a: K230 off; board 29.0 / 29.0 / 29.0 C (AHT20) (2026-11-01) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 122816 / min 96536 / 122816 B, blocks 108 -> 108, deleted 0; leak: n/a, 1 snapshots (mem_leak.py needs >= 8 cycles) (2026-11-01) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free` |
| Image (sizes) | BL616 test firmware 1317872 B (2026-11-01, on the board at the run) | minimal image (rule) | the image this run used |
| Compile flags | BL616 test firmware -Os -g (SDK Kconfig default), sections, --gc-sections, no LTO | smallest code (rule) | |
| Features over the minimal image | as test106, plus pt_fault (test firmware only) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

n/a: no K230 boot; the BL616 runs throughout.

## Battery energy

n/a: setup is on USB power.
