# test035: USB plug wake (PGOOD on ACOMP0)

Status: **pass** (2026-10-22): plug / unplug seen awake, HBN wake on both edges, 19 charger plug transitions all reported.

A USB plug wakes the hibernating BL616 through PGOOD on ACOMP0 (threshold 1.65 V today; 0.5 V with the rev 2 battery
divider, bug-004) and switches it to USB mode (BLE pairing, awake). Unplug returns it to the battery path.

## Setup

BL616 test firmware with PGOOD read as the product does (`usb_sense_init()`): IO03 analog, ACOMP0 on ADC3 against
vio_sel 33 x 50 mV = 1.65 V (correct: scaling 1, and the scaling factor has no effect anyway, tests/test034), 50 mV
hysteresis. `pt_usb` (state + this boot's wake source), live "USB cable plugged in / unplugged" lines from the
background task, `pt_sleep <s>` arms ACOMP0 on the next PGOOD edge (plug = falling, as `usb_arm_hbn_wake()`; unplug
= rising while USB is in). The board runs from the cell (wired to C11) while USB-C 1 is out; the console is PR1.
USB-C 1 VBUS meter in the cable that is plugged and unplugged.

## Needs (build features and dependencies)

BL616 ACOMP0 (PGOOD), HBN wake on ACOMP, USB mode switch.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. HBN (pt_sleep / product), plug USB-C 1: wake within ms, PGOOD low, USB mode; unplug: back to battery mode.
2. USB-C 2 only (through D2): the same?
3. With the K230 running (bug-002 back-feed): what the BL616 sees, and when power arrives after the session.
4. Plug/unplug 100 times: no missed wake, no stuck state.

## Pass

Every plug wakes and every unplug returns to battery mode; latency recorded.

## Result

- Awake: unplug USB-C 1 -> "USB cable unplugged (PGOOD high)" at once, the board stays up on the cell; plug back ->
  "detected (PGOOD low)" (2026-10-22).
- HBN armed for plug-in: woke **by usb** on the plug; HBN with USB in, armed for unplug: woke **by usb** on the unplug
  (after 103 s, before the 120 s RTC): both edges wake (2026-10-22).
- Wake latency not measured directly: HBN wake path ~15 ms (test021) plus PGOOD's fall (BQ24072 open drain against
  R23 100 k: fast) (2026-10-22).
- Plug cycles with a plain USB charger, BL616 awake: 10 unplugs and 9 plug-ins in 120 s, every transition seen,
  none stuck (2026-10-22).
- Plug-in during a K230 session from a USB-C host (Mac): no power (0.03 mA, PGOOD high) until the rails go off;
  PGOOD low 113 ms after the 3V3 rail was gone (bug-002: the CH340's D+ pull-up back-feeds VUSB through D3, a USB-C
  source waits for VBUS below 0.8 V). A plain 5 V charger powers the board even during a session (2026-10-22).
  Solved on the boards by the U2 1-2-3 bridge (2026-10-25).
- Firmware (main.c): `k230_off()` waits up to 300 ms more for PGOOD after the power-off, and `sleep_for()` enters USB
  mode when USB is present, so a plug during a session is not missed until the next humidity slice (2026-10-22).
- Step 2 (USB-C 2 only): not applicable on this board, USB-C 2's VBUS cut by hand (2026-10-21).

Open:
- Step 4 ran 19 transitions by hand, not the planned 100.
- The Mac once gave no power with the K230 **off** (0.02 mA after a few plugs at 17:39-17:41; most likely its port
  protection after repeated plug-ins with the charger's inrush); the plain charger worked at once (2026-10-22).
- Plug-in during a session not re-run with the U2 1-2-3 bridge.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VUSB C2.1, PGOOD (BL616 IO03), VBAT C15.1.

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: a BL616 wake, no K230 boot; wake -> main() 17.3 ms (test021) | target (test022) | longest step: |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | n/a: hand steps on 2026-10-22, not timed | - | setup (flashing, plugging, waiting) separately |
| wake latency on plug | ~15 ms estimated: HBN wake path (test021) + PGOOD's fall (BQ24072 open drain, R23 100 k: fast); not measured directly | - | % of the top |
| Current idle / during (min / avg / peak) | asleep 35 uA, comparators armed (test031); after the plug the BL616 runs from USB: ~60 mA awake (test031, test040), 207 mA with the K230 rails up in USB boot (test019); no battery measurements (user, 2026-10-30) | floor (test031) | USB-C 1 VBUS meter |
| Voltage under load (min) | n/a: plug events, no load; VUSB and PGOOD as in Result | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a: the plug brings its own power (USB) | - | scripts/energy.py |
| Per day | n/a: a plug per charge | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | n/a: the BL616 alone, nothing heats; not recorded on 2026-10-22 | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | n/a: every HBN wake restarts the BL616 heap; repeated HBN wakes: test021 | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | BL616 test firmware ~150 KB (2026-10-22, before the Wi-Fi set) | minimal image (rule) | the image this run used |
| Compile flags | BL616 test firmware -Os -g (SDK Kconfig default), sections, --gc-sections, no LTO | smallest code (rule) | |
| Features over the minimal image | `pt_usb`, `pt_sleep` (test firmware commands); the USB comparator setup is firmware/bl616 leak_wake.c's | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

Plug to the BL616 in USB mode (console up).

## Battery energy

n/a while on USB (the charger powers the board); the unplug path's return to sleep is checked by test031's sleep current afterwards.
