# test030: sleep current (BL616 hibernating, K230 off)

Status: **planned** (2026-10-21).

What the board draws between wakes, which is most of its life. ENERGY.txt estimates ~15-20 uA (BL616 HBN "a few
uA", U5 TPS63802 ~11 uA, probe divider 1.65 uA, AHT20, charger); this test measures it, part by part. It needs no
product firmware: a `pt_sleep <s>` command in the test firmware enters the same hibernate the product uses (HBN level
0, `pm_hbn_mode_enter`) with an RTC wake after <s> seconds.

## Setup

- Cell charged; **USB-C 1 and USB-C 2 unplugged**; PR1 unplugged once the command is sent (its USB lines would keep
  the BL616's USB PHY and the host's pull-ups in the circuit).
- Meter in series in the cell's + lead (cell + -> meter mA/uA jack, COM -> JB1 pin 1), **uA range** for sleep, mA for
  the awake states; mind the burden: switch to the mA/A input before the BL616 wakes (or give pt_sleep long enough).
- BL616 test firmware with `pt_sleep` (to add: HBN level 0, RTC wake, the K230 side pins parked, AHT20 idle).

## Steps

| State | How | Expected |
|---|---|---|
| BL616 awake, idle, K230 rails off | test firmware at its prompt | a few mA |
| BL616 in HBN level 0 | `pt_sleep 120`, unplug PR1, read after ~10 s | the board's sleep current |
| BL616 held in reset | PR1 pad 5 (EN) to GND | the rest of the board: U5, divider, AHT20, charger |
| Leak probes wet | S1 shorted while asleep | + the divider's wet share; an ACOMP wake in the product |
| After the RTC wake | console back after <s> | wake works, time to console |

Then: BL616 HBN share = (HBN) - (EN low); compare each part with ENERGY.txt section 2.

## Boot time

The BL616's HBN wake to its first instruction and to the console (RTC wake after <s>): part of every humidity slice
and every K230 session start (with test020).

## Battery energy

This test is the sleep figure: uA -> mAh per month (uA x 0.72), and the share of a 1000 mAh cell per month; it goes
into ENERGY.txt section 2 and the budget line "Sleep".

## Pass

Sleep current measured and split into the BL616 and the rest; within the ENERGY.txt estimate, or the largest consumer
named with a fix (firmware: HBN level, pins; rev 2: parts).
