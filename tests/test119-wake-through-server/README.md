# test119: the phone wakes a board's BLE through the server

Status: **setup firmware: the wake and the way back pass (2026-11-02, bench, no phone); the phone steps to re-run** (old pass 2026-11-01, 12/12, on the full firmware's own BLE;
To re-prove with the setup firmware (user, 2026-11-02: the full firmware has no BLE, DESIGN.md 5.3): "ble":1 in the
check-in answer makes the full firmware switch to the setup firmware (slot.c, tests/test126), which advertises; after
the phone's setup, or 2 minutes without one, back to the full firmware. The pass below is the full firmware's old BLE.

A configured board present at the server does not advertise on battery. To change its Wi-Fi, a phone standing next to
it wakes it through the server: the app shows a present board out of BLE range with a **Wake** button; the tap asks the
server (POST /v1/devices/<id>/wake), the board's next check-in answer carries "ble":1 and it advertises for 2 minutes;
the app finds it (it scans every 10 s while it waits) and the button turns into **Change the Wi-Fi**. Free for the
battery until asked: the check-ins happen anyway. The latency is up to one check-in interval. The server refuses a
second wake within 2 minutes (429), so wakes cannot drain the battery. Pairing still needs the PIN. A board without
presence cannot hear the server: it advertises by itself when its presence is lost (test117).

## Setup

- Full firmware (firmware/bl616_full, make flash), the PIN stored, a network stored; USB-C 1 in; the phone on adb,
  unlocked. `provision battery on`: the battery rule on USB power, so the radio is as on battery.
- The server with the wake endpoint (server/deploy.sh); the runner sets its poll interval to 20 s and puts back 60 s.

## Needs (build features and dependencies)

As test111 (BLE provisioning, the Wi-Fi check, presence); the server's wake; the advertising rule (provision.c).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps (run.py)

1. Present, silent on battery; the app shows the board with the Wi-Fi icon, no Bluetooth icon, a Wake button.
2. Wake tapped: the server takes it (200); the board woken at its next check-in (`provision`: "on (woken by a phone)");
   the app shows the Bluetooth icon and "Change the Wi-Fi".
3. The Wi-Fi changed from the app: Done.
4. A second wake within 2 minutes: 429.
5. After the 2 minutes: silent again.

## Pass

The Wi-Fi of a present, silent board is changed from a phone next to it within one check-in interval plus the setup,
with nothing by hand on the board.

## Result

- 2026-11-02 17:52 UTC, the product pair (full + setup, 0.6+g1d2b375-dirty-202611021742), USB-C 1 in, no phone:
  `switch_to_setup wake` (what "ble":1 in the check-in answer does, firmware/bl616_full/presence.c) -> the setup
  firmware's console 5.2 s later (slot 1), awake on USB power, the PIN and the network readable (`provision`) -> **back
  in the full firmware 127.1 s after the command by itself** (the 2-minute timer + the start) -> its check-in reached
  the server (0x03). Open: the real "ble":1 from the server's wake (POST /v1/devices/{id}/wake) and the phone finding
  and setting the board in those 2 minutes (the runner drives the old `provision battery` rule: to rework).

2026-11-01, 12/12 checks (poll 20 s, `provision battery on`): the present board out of range shows Wake (after the first
scan missed it); the tap -> the server 200 -> the board woken at its next check-in 6 s later -> the app shows the
Bluetooth icon and "Change the Wi-Fi" at once; the Wi-Fi changed (Done 8.4 s); a second wake 429; silent again
after the 2-minute window.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the BL616 alone, K230 off | target (test022) | |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | 211 s automated (run.py, 2026-11-01), no setup | - | |
| Wake tap -> the button changes the Wi-Fi | Wake tap -> the button changes the Wi-Fi 9 s (poll 20 s: up to one interval) | - | % of the top |
| Current idle / during (min / avg / peak) | n/a: on USB power, no meter reading (2026-11-02 bench run; the battery case is the setup firmware's, test120) | floor (test031) | USB-C 1 meter (LEAKCAM_METER=ask) |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: on USB power; a switch is one reset and a 4 KB partition-table write (test126) | - | |
| Per day | n/a: wakes on request | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | n/a: K230 off, the BL616 at idle load | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 146012 / min 120100 / 146012 B, blocks 106 -> 106, deleted 0; leak: n/a, 1 snapshots (mem_leak.py needs >= 8 cycles) (2026-11-01) | no leak | |
| Image (sizes) | BL616 test firmware 1317888 B (2026-11-01, on the board at the run) | minimal image (rule) | |
| Compile flags | BL616 product (test compiles: no -O3/LTO) | smallest code (rule) | |
| Features over the minimal image | as test111 | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

n/a: no K230 boot.

## Battery energy

A wake costs 2 minutes awake (~62 mA, ENERGY.txt), at most every 2 minutes (the server refuses more): ~2 mAh per wake.
