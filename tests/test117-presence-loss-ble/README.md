# test117: presence lost brings BLE back (the phone sets the Wi-Fi at once)

Status: **pass** (2026-11-03, 19/19, run.py on the product pair, the phone 8 m away in another room): every lost
presence switched the full firmware to the setup firmware in 9-13 s, the phone found the board at once (RSSI -70) and
set the network out of it in 8.7-9.1 s, back in the full firmware 4 s after Done (old pass 2026-11-01, 13/13, on the
full firmware's own BLE and the `provision battery` rule, both gone).

A LEAKCAM with a network stored runs the full firmware, which has no BLE (DESIGN.md 5.3.1). When a check-in does not
reach the server, for whatever reason (the network gone, a wrong password saved without checking, no internet, the
server down), the presence is lost after three failed check-ins in a row (presence.c MISSED_MAX: no flapping, user
2026-11-04; before, at once) and the full firmware switches to the setup firmware (switch_to_setup("lost")), which advertises (100 ms awake, 2 s asleep on battery), so a phone nearby finds it and sets
the Wi-Fi; the stored network switches it back to the full firmware, which checks in at once. The setup firmware stays
with the cause gone (the server back): only a setup or its 6 h retry (test125) ends it.

## Setup

- The product pair (firmware/bl616_full in slot 0, bl616_setup in slot 1: `make flash SLOT1_BIN=...`), the PIN
  stored, a network stored; USB-C 1 in; the phone on adb, unlocked.
- The presence server on the Pi: the runner sets its poll interval to 10 s, stops and starts it, and puts back 60 s.

The runner reads the firmware from `slot` (leakcam-build full / setup); the phone's scan shows the radio itself (none
in the full firmware). On battery the setup firmware advertises every 2 s (test120, test106).

## Needs (build features and dependencies)

As test111: BLE provisioning, the Wi-Fi check (setup firmware), presence and the slot switch both ways (test126).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps (run.py)

1. Set up from the app: back in the full firmware, present; the phone does not find the board (no BLE there).
2. The server down: the setup firmware within one interval; the phone finds the board (its serial) at once; the server
   back: the setup firmware stays; the app sets the network: the full firmware, present.
3. A missing network saved without checking from the app: the full firmware cannot check in, the setup firmware at
   once; the app sets the real network at once (Done): the full firmware, present at the server, no BLE.

## On battery (`run.py --battery`, added 2026-11-03, not run yet)

Steps 2 and 3 with USB-C 1 out (hands: out after step 1, in at the end): the setup firmware sleeps with its console
off, so the phone's scan stands for "the setup firmware"; the full firmware keeps its console on battery. Why: the run
above was on USB, where the setup firmware never sleeps (bugs/bug-006).

## Pass

Every cause of a lost presence switches to the setup firmware within one interval (at once after new credentials), the
phone finds the board and sets the Wi-Fi in one go, and a present board runs the full firmware (no BLE).

## Result

- 2026-11-03 05:25-05:28 UTC, 19/19, run.py 189 s, the product pair (full 0.6+g1d2b375-dirty-202611021742, setup 0.6+g1d2b375-dirty-202611022115), USB-C 1 in, the phone 8 m away in
  another room: server down -> the setup firmware 9 s later, found by the phone in 0.1 s at RSSI -70; with the server
  back still the setup firmware 20 s later; set up 9.1 s, back in the full firmware 4 s, present 16 s after the setup
  started. A missing network saved without checking (15.0 s) -> the setup firmware 13 s after the switch back, found in
  0.2 s; the real network 8.7 s, present 15 s after; no BLE again.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the BL616 alone, K230 off | target (test022) | |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | 189 s automated (run.py, 2026-11-03), no setup | - | |
| presence lost -> the setup firmware; set up again from the app | presence lost -> the setup firmware 9 s (server down) / 13 s (network gone), poll 10 s; set up again from the app 8.7-9.1 s, the full firmware 4 s after Done (2026-11-03) | within one interval | |
| Current idle / during (min / avg / peak) | n/a: on USB power, no meter reading (2026-11-02 bench run; the battery case is the setup firmware's, test120) | floor (test031) | USB-C 1 meter (LEAKCAM_METER=ask) |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: on USB power; a switch is one reset and a 4 KB partition-table write (test126) | - | |
| Per day | n/a: a fault case | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | n/a: K230 off, the BL616 at idle load | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | full firmware after the last setup's reset: BL616 free 210052 B (2026-11-03; fresh boots, no heap carries over a setup: no leak test) | no leak | |
| Image (sizes) | full 1085760 B, setup 1284464 B (2026-11-03, the pair on the board) | minimal image (rule) | |
| Compile flags | BL616 product (test compiles: no -O3/LTO) | smallest code (rule) | |
| Features over the minimal image | as test111 | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

n/a: no K230 boot.

## Battery energy

Advertising after a loss is the setup firmware's (PDS sleep, 2 s advertising: test120); a switch is one reset and a 4 KB partition-table write (test126).
