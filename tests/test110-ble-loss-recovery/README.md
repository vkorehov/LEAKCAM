# test110: setup app recovery from loss at every step

Status: **fail, 31/34** (2026-11-01, run.py on the BL616 test firmware): lost notifications recovered (STATUS reads);
link loss and Wi-Fi loss named in 4-10 s; **open**: a link lost with the result leaves the app saying "nothing
stored" while the board stored it; the phone's Bluetooth off waits out the 60 s timeout; an Android connect failure
(GATT 133) is not retried.

Every interaction of a Wi-Fi setup from the phone (test106) with a loss injected into it: the app must either recover
by itself or tell the user a named failure in time, its message must match what the board stored (eventual
consistency: STATUS is the board's state and the app reconciles with it), and a clean setup right after must work.

## Setup

- BL616 test firmware (tests/firmware/bl616_powertest) with `pt_fault`: linker wraps around the product's
  provisioning (firmware/bl616/ble_pairing.c, wifi_check.c), the product sources unchanged. BLE retransmits on the
  link layer, so a loss there is a dropped notification (host buffers full) or a dropped link (supervision timeout);
  on Wi-Fi a DNS or HTTP answer that never comes.
- Android phone with the LEAKCAM Setup app on adb (test106 Setup), unlocked, Bluetooth on; USB-C 1 in.
- The test network from `~/.leakcam_wifi`; a decoy network ("t110-decoy") stored unchecked before each case, so the
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
6. After every case: the fault off, a clean setup Done (recovery); BL616 memory sampled at the end.

## Pass

No case leaves the user waiting for a timeout when the board knows the answer, no case ends with the app and the
board disagreeing, and every case is followed by a working setup.

## Result

run.py, 2026-11-01 07:46, BL616 test firmware with pt_fault + the setup app with STATUS reads (6b6b51e), bench phone
(Samsung, Android 13): 31/34, 605 s; crash record none before and after; BL616 free 122816 B at the end.

- Lost notifications (every one: 5 dropped; every second: 2): Done in 7.0 / 7.5 s, app and board agree.
- Link lost at connect, at joining, while getting an address, while reaching the internet: named in 4.1-7.5 s, nothing
  stored on the board, app and board agree. The joining case says "refused the save command (error 133)": the commit
  write failed with the link, nothing was refused (wording).
- **Link lost with the result: the board stored the real network (store() runs before the result notification), the
  app said "Nothing was stored"**: the only disagreement. The app claims what it cannot know; it must reconnect and
  read the board's state.
- DNS / HTTP answer lost: "names do not resolve" / "cannot reach the internet", decoy kept.
- **Phone Bluetooth off for 4 s during the check: the app waited 66.1 s for "did not answer"**: Android gave no
  disconnect callback; the app does not watch the adapter state.
- **GATT 133 on the second connection of a save-without-checking** (no fault armed: a real Android connect failure)
  ended that setup with "disconnected"; the app does not retry a failed connect. Case "result notification lost" was
  skipped (its decoy did not store).
- Every clean setup after a case: Done, 6.9-7.9 s.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: none (on USB; the setup's current is test040's).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: setup runs on the BL616 alone, K230 off | target (test022) | longest step: |
| This part's boot share | n/a: no K230 boot in setup | - | |
| Test run time | 605 s automated (run.py, 2026-11-01), no setup | - | setup (flashing, plugging, waiting) separately |
| recovery: per case app message, time, board state | 2026-11-01: notify all 7.0 s Done; notify half 7.5 s Done; notify final not run (decoy failed, status 133); link at connect 4.1 s disconnected (133); link at joining 6.5 s "refused the save command (133)"; link at address 7.1 s / internet 7.5 s "dropped, nothing stored", board decoy; **link with the result 10.1 s "nothing stored", board stored the real network**; DNS 6.8 s / HTTP 7.4 s named, decoy kept; **phone Bluetooth off 66.1 s "did not answer"**; every clean setup after 6.9-7.9 s | - | % of the top |
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
