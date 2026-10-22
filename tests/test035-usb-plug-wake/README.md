# test035: USB plug wake (PGOOD on ACOMP0)

Status: **pass with opens** (2026-10-22): PGOOD on ACOMP0 follows plug / unplug of USB-C 1; HBN wake on plug-in and on unplug works (woke by usb, both edges). Open: USB-C 2 path, plug during a K230 session, 100-cycle run.

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

## Results

### 2026-10-22

| Test | Result |
|---|---|
| awake, unplug USB-C 1 | "USB cable unplugged (PGOOD high)" at once; the board stays up on the cell, PR1 console unaffected (PR1 does not feed PGOOD) |
| awake, plug back | "detected (PGOOD low)" |
| HBN, USB out, armed for plug-in; plugged ~10 s later | woke **by usb**, console back 11 s after the sleep started (plug + VMware click) |
| HBN, USB in, armed for unplug (an accident: USB was back before the sleep); unplugged | woke **by usb** after 103 s, before the 120 s RTC: the unplug edge wakes it too |

Wake latency: not measured directly; the HBN wake path is ~15 ms (test021) plus PGOOD's fall (BQ24072 open drain
against R23 100 k: fast) - well inside "within ms" of the USB power being good.

Open: step 2 (USB-C 2 only: its VBUS was cut by hand on 2026-10-21, so it powers nothing on this board); step 3 (plug
during a K230 session, bug-002 back-feed); step 4 (100 plug cycles by hand); the product firmware build (rewrite).

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VUSB C2.1, PGOOD (BL616 IO03), VBAT C15.1.

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test022) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| wake latency on plug | not measured yet | - | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test031) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | to fill | minimal image (rule) | the image this run used |
| Compile flags | to fill | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | to fill: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

Plug to the BL616 in USB mode (console up).

## Battery energy

n/a while on USB (the charger powers the board); the unplug path's return to sleep is checked by test031's sleep current afterwards.

## Obstacles

- **First plug-wake try armed the wrong edge**: the user plugged back within the 2-3 s between my unplug check and
  the sleep command, so the firmware saw USB present and armed the unplug edge -> the script waits for 5 s of
  "unplugged" first and the user counts to 15 before plugging. (It proved the unplug wake instead.)
- **"woke by leak" after a flash**: the HBN interrupt state survives the flasher's reset, an ACOMP1 event latched while
  awake showed as a wake reason. The product clears all three at boot and before each sleep; harmless.

