# test035: USB plug wake (PGOOD on ACOMP0)

Status: **pass** (2026-10-22): plug / unplug seen awake, HBN wake on both edges, 19 charger plug transitions all reported; a plug-in during a K230 session gets no power from a USB-C host until the rails go off (bug-002, PGOOD 113 ms later) - product handles it now. Open: the Mac refusing power with the K230 off once.

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

**Step 3, plug-in during a K230 session (K230 running from NAND on the cell, USB-C 1 from a Mac):** 0.03 mA in VBUS,
PGOOD high, no charging - also with the meter leads shorted. `pt_off` with the cable in: charging at once.
`pt_usb offwait` (rails off, then time PGOOD): **PGOOD low 113 ms after the 3V3 rail was gone**. This is bug-002 (the
CH340's D+ pull-up back-feeds VUSB through D3; a USB-C source waits for VBUS below 0.8 V before turning it on).
Product consequence, fixed in main.c: after a session the USB power arrives only when the rails are off, so the plug
edge has already passed when `sleep_for()` arms it - the board slept on battery while plugged until the next
humidity slice (up to 10 min). Now `k230_off()` waits up to 300 ms more for PGOOD after the power-off, and
`sleep_for()` enters USB mode when USB is present. A plain 5 V charger powers the board even during a session.

**Step 4, plug cycles (plain USB charger, BL616 awake):** 120 s window, 10 unplugs and 9 plug-ins reported, ending
unplugged: every transition seen, none stuck (19 by hand, not the planned 100).

**Step 2 (USB-C 2 only):** not applicable on this board: USB-C 2's VBUS was cut by hand on 2026-10-21.

Open: the Mac once gave no power with the K230 **off** (0.02 mA after a few plugs at 17:39-17:41; most likely its
port protection after repeated plug-ins with the charger's inrush), the plain charger worked at once; the product
firmware build (rewrite).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VUSB C2.1, PGOOD (BL616 IO03), VBAT C15.1.

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | **MISSING** | target (test022) | longest step: |
| This part's boot share | **MISSING** | - | |
| Test run time | **MISSING** | - | setup (flashing, plugging, waiting) separately |
| wake latency on plug | **MISSING** | - | % of the top |
k5 mA at 17:29 after the plug-in (BQ24072 charging the cell it ran on while unplugged; BL616 awake, K230 off) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING** | - | scripts/energy.py |
| Per day | **MISSING** | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | **MISSING** | minimal image (rule) | the image this run used |
| Compile flags | **MISSING** | smallest code (rule) | |
| Features over the minimal image | **MISSING**: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | **MISSING**: speed / size / boot / current vs the current flags | the faster one stays | |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 VBUS meter |

## Boot time

Plug to the BL616 in USB mode (console up).

## Battery energy

n/a while on USB (the charger powers the board); the unplug path's return to sleep is checked by test031's sleep current afterwards.

## Obstacles

- **First plug-wake try armed the wrong edge**: the user plugged back within the 2-3 s between my unplug check and
  the sleep command, so the firmware saw USB present and armed the unplug edge -> the script waits for 5 s of
  "unplugged" first and the user counts to 15 before plugging. (It proved the unplug wake instead.)
- **The Mac stopped powering the port after several plug-ins** (K230 off): switched to a plain USB charger (user).
- **The user's first "20 cycles" window passed before the cycling started** -> the count ran again and caught it.
- **"woke by leak" after a flash**: the HBN interrupt state survives the flasher's reset, an ACOMP1 event latched while
  awake showed as a wake reason. The product clears all three at boot and before each sleep; harmless.
