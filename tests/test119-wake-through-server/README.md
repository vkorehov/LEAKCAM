# test119: the phone wakes a board's BLE through the server

Status: **pass** (2026-11-03, 16/16, run.py on the product pair, the phone 8 m away in another room): the Wake tap ->
the full firmware switched to the setup firmware at its next check-in 11 s later -> the app showed it in range and
changed the Wi-Fi (8.8 s), back in the full firmware 4 s after; a second wake refused (429); a wake nobody followed:
120 s in the setup firmware, then back by itself (old pass 2026-11-01, 12/12, on the full firmware's own BLE).

A configured board present at the server runs the full firmware, which has no BLE (DESIGN.md 5.3.1). To change its Wi-Fi, a phone standing next to
it wakes it through the server: the app shows a present board out of BLE range with a **Wake** button; the tap asks the
server (POST /v1/devices/<id>/wake), the board's next check-in answer carries "ble":1 and the full firmware switches to the setup firmware ("wake":
firmware/bl616_full/presence.c), which advertises; after the phone's setup, or 2 minutes without one, back to the full
firmware;
the app finds it (it scans every 10 s while it waits) and the button turns into **Change the Wi-Fi**. Free for the battery until asked: the check-ins happen anyway. The latency is up to one check-in interval. The server refuses a
second wake within 2 minutes (429), so wakes cannot drain the battery. Pairing still needs the PIN. A board without
presence cannot hear the server: it switches to the setup firmware by itself when its presence is lost (test117).

## Setup

- The product pair (firmware/bl616_full in slot 0, bl616_setup in slot 1), the PIN stored, a network stored; USB-C 1
  in; the phone on adb, unlocked, nobody using it (another app in front hides the Wake button from the runner's UI
  dump). The runner reads the firmware from `slot`.
- The server with the wake endpoint (server/deploy.sh); the runner sets the board's check-in interval to 20 s per
  device with `PUT /v1/devices/<id>/intervals` (phone.intervals), reads `check-in every N s` from the board's
  `presence`, and sets 60 s back at the end (product default 3600 s, the bench server's default 60 s).

## Needs (build features and dependencies)

As test111 (BLE provisioning, the Wi-Fi check, presence); the server's wake; the slot switch both ways (test126).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps (run.py)

1. Present, the full firmware; the app shows the board with the Wi-Fi icon, no Bluetooth icon, a Wake button.
2. Wake tapped: the server takes it (200); the setup firmware at the board's next check-in; the app shows the
   Bluetooth icon and "Change the Wi-Fi".
3. The Wi-Fi changed from the app: Done; back in the full firmware.
4. A second wake within 2 minutes (of the one delivered): 429.
5. A wake after the 2 minutes, nobody follows it: the setup firmware, back in the full firmware by itself after 2
   minutes.

## On battery (`run.py --battery`, added 2026-11-03, not run yet)

Steps 2-5 with USB-C 1 out (hands: out before the wake, in at the end): the woken setup firmware sleeps with its
console off, so the app's Bluetooth icon and the phone's scan stand for it; the unanswered wake's 2 minutes timed from
the phone's find (up to one 20 s scan late). Why: the run above was on USB, where the setup firmware never sleeps
(bugs/bug-006).

## Pass

The Wi-Fi of a present, silent board is changed from a phone next to it within one check-in interval plus the setup,
with nothing by hand on the board.

## Result

- 2026-11-03 05:42-05:48 UTC, 16/16, run.py 358 s, the product pair (full 0.6+g1d2b375-dirty-202611021742, setup
  0.6+g1d2b375-dirty-202611022115), check-in every 20 s, USB-C 1 in, the phone 8 m away in another room: the tap -> the server
  200 -> the setup firmware 11 s later -> the app showed the Bluetooth icon and "Change the Wi-Fi" at once -> Done 8.8 s
  -> the full firmware 4 s later; a second wake 429; the next wake 2 minutes after the delivered one -> the setup
  firmware 23 s later (one interval), back in the full firmware by itself 120 s after. Three runs before it failed on
  the runner and the bench (HISTORY.md Obstacles).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the BL616 alone, K230 off | target (test022) | |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | 358 s automated (run.py, 2026-11-03), no setup | - | |
| Wake tap -> the button changes the Wi-Fi | Wake tap -> the button changes the Wi-Fi 11 s (check-in every 20 s: up to one interval); an unanswered wake back in the full firmware after 120 s (2026-11-03) | - | % of the top |
| Current idle / during (min / avg / peak) | n/a: on USB power, no meter reading (2026-11-02 bench run; the battery case is the setup firmware's, test120) | floor (test031) | USB-C 1 meter (LEAKCAM_METER=ask) |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: on USB power; a switch is one reset and a 4 KB partition-table write (test126) | - | |
| Per day | n/a: wakes on request | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | n/a: K230 off, the BL616 at idle load | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | full firmware after the run: BL616 free 209680 B (2026-11-03; each switch is a fresh boot: no leak test) | no leak | |
| Image (sizes) | full 1085760 B, setup 1284464 B (2026-11-03, the pair on the board) | minimal image (rule) | |
| Compile flags | BL616 product (test compiles: no -O3/LTO) | smallest code (rule) | |
| Features over the minimal image | as test111 | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

n/a: no K230 boot.

## Battery energy

A wake costs 2 minutes awake (~62 mA, ENERGY.txt), at most every 2 minutes (the server refuses more): ~2 mAh per wake.
