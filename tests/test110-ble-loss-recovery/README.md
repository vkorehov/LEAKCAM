# test110: setup app recovery from loss at every step

Status: **pass on the product pair and app** (2026-11-08, run.py, ~4 min): phone Bluetooth off 4 s before the save / during the check and the board reset during the check each end in a named result within 28 s and the app never contradicts the board (Done = stored; "cannot tell whether the network was stored" when the link was lost after the commit); every case is followed by a clean setup, Done 7.8-9.0 s. The test-bed fault cases of 2026-11-03 are in HISTORY.md.
Bluetooth off before the save, DNS / HTTP loss: named in 6.4-8.9 s; app and board agree in every case.

Every interaction of a Wi-Fi setup from the phone (test106) with a loss injected into it: the app must either recover
by itself or tell the user a named failure in time, its message must match what the board stored (eventual
consistency: STATUS is the board's state and the app reconciles with it), and a clean setup right after must work.

## Setup

- The setup firmware's test bed (tests/firmware/bl616_setup_test, make flash there; since 2026-11-03, before: the
  power test firmware): firmware/bl616_setup's own sources started by `lp_boot`, its switch to the full firmware off
  (`lp_switch off`), with `pt_fault`: linker wraps around the product's provisioning (ble_pairing.c,
  wifi_check.c), the product sources unchanged. BLE retransmits on the
  link layer, so a loss there is a dropped notification (host buffers full) or a dropped link (supervision timeout);
  on Wi-Fi a DNS or HTTP answer that never comes.
- Android phone with the LEAKCAM Setup app on adb (test106 Setup), unlocked, Bluetooth on; USB-C 1 in.
- The test network from `~/.leakcam_wifi`; a decoy network ("t110-decoy") stored directly by the test bed (`pt_ble decoy`, under a second; through the
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

run.py, 2026-11-03 05:50-05:57 UTC, the setup firmware's test bed (setup_test 0.6+g8828b50-dirty-202611030525:
firmware/bl616_setup's sources + pt_fault) + the setup app, bench phone (Samsung, Android 13) 8 m away in another
room: 30/30, 423 s; BL616 free 160988 B at the end.

| Case | App, time | Board |
|---|---|---|
| every notification lost (4) / every second (2) / the result's (1) | Done 9.8 / 9.4 / 11.9 s | real network |
| link lost at every connect (6 faults = 3 connects) | "disconnected (133)", 6.4 s | decoy |
| link lost when the check starts / getting an address / reaching the internet / with the result | Done 10.1 / 11.5 / 11.4 / 11.6 s (reconnect + STATUS read) | real network |
| DNS / HTTP answer lost | named, 8.9 / 8.3 s | decoy |
| phone Bluetooth off 4 s before the save | "Bluetooth is off", 7.9 s | decoy |
| phone Bluetooth off 4 s during the check | Done 15.2 s (reconnected when it came back) | real network |

Every clean setup after a case: Done, 8.2-9.9 s (baseline 8.9 s). Each Done is 1-2 s longer than in the 2026-11-01 run
on the test firmware (HISTORY.md); not traced: the check now ends at the API (DESIGN.md 5.3.1), the phone is 8 m away.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: none (on USB; the setup's current is test040's).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: setup runs on the BL616 alone, K230 off | target (test022) | longest step: |
| This part's boot share | n/a: no K230 boot in setup | - | |
| Test run time | 423 s automated (run.py, 2026-11-03), no setup | - | setup (flashing, plugging, waiting) separately |
| recovery: per case app message, time, board state | 2026-11-03: notifications lost -> Done 9.4-11.9 s; link lost after the save (4 points) -> Done 10.1-11.6 s; Bluetooth off during the check -> Done 15.2 s; link lost on every connect 6.4 s, Bluetooth off before the save 7.9 s, DNS / HTTP 8.9 / 8.3 s: named | - | % of the top |
| Current idle / during (min / avg / peak) | n/a: not metered; the same BLE + check path in test040 on USB: advertising 63.4, connected 63.1, check peak 86 mA (2026-10-29) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | n/a: the bench runs on USB power | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a for the cell: setup draws from USB | - | scripts/energy.py |
| Per day | n/a: once per installation, on USB | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die n/a: K230 off; board 29.0 / 29.0 / 29.0 C (AHT20) (2026-11-01) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 160988 / min 148076 B at the end, blocks 102; leak: n/a, 1 snapshot (one per run; test106 repeats) (2026-11-03) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free` |
| Image (sizes) | BL616 setup test bed 1288672 B (2026-11-03, on the board at the run) | minimal image (rule) | the image this run used |
| Compile flags | BL616 setup test bed -Os -g (SDK Kconfig default), sections, --gc-sections, no LTO | smallest code (rule) | |
| Features over the minimal image | as test106, plus pt_fault (test bed only) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

n/a: no K230 boot; the BL616 runs throughout.

## Battery energy

n/a: setup is on USB power.
