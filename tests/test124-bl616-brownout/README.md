# test124: the BL616's own brown-out protection

Status: **planned** (2026-11-01, user: "separate low level test for brown out protection on bl616 itself"). Design:
DESIGN.md 5.3.1 (the battery's lowest voltage: the cell's protection board at ~2.4 V; the BL616's brown-out handles
the collapse; bug-004 accepted).

When the cell's protection board cuts (or the cell sags under load), 3V3_SLEEP collapses and the BL616 must reset
cleanly instead of running erratically: no flash write torn, no latch-up, a clean start when the supply comes back.
The SDK's startup enables the chip's brown-out detector in every build (system_bl616.c `System_BOD_Init()`: on,
threshold 2.40 V on its own supply, reset on trip, no interrupt). This test proves it alone, before test038 and
test122 rely on it.

## Setup

- Steps 1-2 on today's board: any LEAKCAM firmware with a read-only command for the BOD configuration and the reset
  record (the setup firmware's test bed or the power test firmware).
- Steps 3-6 need a lab supply (none on the bench): **on the next assembled board** (user, 2026-10-31: battery and supply
  work there), the supply in place of the cell on JB1 (current limit 1 A), USB out, the meter on 3V3_SLEEP (U5's
  output) and the supply current.

## Needs (build features and dependencies)

BL616: HBN BOD registers (bl616_hbn.c HBN_Set_BOD_Cfg / HBN_Get_BOD_OUT_State), the reset record
(GLB_Get_Reset_Reason, wdg_guard.c already reads it), the crash recorder's reset report; easyflash (a write in flight).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Configuration: read the HBN BOD register back from the running firmware: enabled, threshold 2.40 V (7), reset on
   trip (POR relevant), interrupt off; in the setup and the full firmware both (nothing later turns it off).
2. The reset record after each kind of reset we can make today (power-on, EN pin, software, watchdog): which bits each
   sets, so a brown-out's own signature can be told apart in step 3.
3. Trip point: supply on JB1 down in 50 mV steps from 3.0 V: where U5 stops holding 3V3_SLEEP, the 3V3_SLEEP voltage at
   which the BL616 resets (expected ~2.40 V), and the reset record after it (its brown-out signature).
4. Slow collapse: supply ramped down over ~1 min to 1.0 V and back up over ~1 min: the BL616 resets once on the way
   down, stays in reset (no chatter: count the resets) and starts cleanly once on the way up; HBN RAM state as expected
   (lost or kept, recorded).
5. A write in flight: easyflash writes looped while the supply drops through the threshold: after the restart the
   key-value store mounts, the last complete value is there (no torn record), no other key lost (repeat 10x).
6. A dip, not a cut: a short sag under load (supply source resistance, or a load step) to just below the threshold
   for 1-10 ms: reset or ride-through, recorded with the minimum 3V3_SLEEP.

## Pass

The brown-out detector is on in both firmwares; the BL616 resets once near 2.40 V of its supply, never latches up,
restarts cleanly, and easyflash survives a write in flight; the reset record names the brown-out.

## Result

Not run.
