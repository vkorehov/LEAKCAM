# test124: the BL616's own brown-out protection

Status: **step 2 pass on the product pair** (2026-11-08, run.py, 61 s): reset records software `rst=80`, watchdog `88`, exception `88` (it resets through the watchdog; the crash record names it), the last one at the server; step 1 (BOD register) proven on the setup test bed 2026-11-02 (HISTORY.md), no product command for it; steps 3-6 need a lab supply.
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

- Step 1, configuration (2026-11-02, `lp_bod` in the setup test bed, the full firmware the same SDK start): HBN BOR_CFG
  0x1f: enabled, threshold 2.40 V (7), reset on trip, interrupt off, comparator out 0 (supply above the threshold).
- Step 2, the reset record (GLB reset recorder, read and cleared at the start by wdg_guard.c, `lp_bod`):
  - after `slot switch` or `reboot` (bl_sys_reset_por): 0x00;
  - after a watchdog reset in the full firmware or the bed without sleep: 0x08 (wdt);
  - after a watchdog reset in the bed after PDS sleeps: 0x7d (wdt, pds, ana_powb, cpu_porst, sys_reset,
    cpu_sys_rstreq): the sleep sets bits of its own, `wdt` is the watchdog's mark;
  - no reset since the start (bed running): 0x80;
  - EN pin: not read yet (the EN taps today were to free a hung board, the record was not read first).
- **The watchdog after a PDS wake (found 2026-11-02, fixed, proven):** a hang after the setup firmware's sleep window was
  never reset and left no record: only EN ended it (three times today). `lp_wdg` (the bed: enable bit, counter, tick):
  after a window the watchdog read enabled but its counter stood still (3 -> 3; before the window 946 -> 226 in
  300 ms). Two causes, both in the wake: the SDK's `board_recovery()` (`peripheral_clock_init_lp()`) rewrites the clock
  gates without the timer's, after the firmware's own wake code (setup_sleep.c `lp_exit`); and PDS loses the timer's
  clock source and divider: with the clock alone ungated it counted fast and reset the chip a few seconds into every
  window (reset record `wdt 1`, no hang record). Fix (firmware/bl616_common/wdg_guard.c `wdg_guard_pause(false)`, run
  after every wake): the watchdog's clock ungated and its whole setup again, then counter reset and start. Proof
  (17:48 UTC): a 20 s window with 12 sleeps and no reset; the counter after it 700 -> 1007 in 300 ms (1 kHz); the
  highest count at a sleep's start 672 (limit 8000), 0 after every wake's restart; `crash hang` after the window ->
  reset by the watchdog 12.4 s later with the record "watchdog hang in task 'shell_task'".
- The record's uptime after a sleep counts from the last wake (the wake restarts the mtimer: "at 12017 ms" for a hang
  ~300 s after the start). Harmless for the decode; noted for test128.
- Open: steps 3-6 (lab supply, next board); the EN pin's reset record; whether HBN RAM (the crash record) survives an EN
  reset (no record after the EN taps, but the record was never written then).

## Metrics

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the BL616 alone, K230 off | target (test022) | |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | steps 1-2 and the watchdog proof ~40 min with three EN taps (before the fix); the proof itself 2 min, no hand step | - | |
| hang -> watchdog reset after a sleep window | 12.4 s (hang detector 6 s + watchdog 8 s from the last feed, minus the feed period) | WDG_GUARD_MS 8 s | before the fix: never |
| Current idle / during (min / avg / peak) | n/a: steps 3-6 (lab supply) | - | |
| Voltage under load (min) | n/a: steps 3-6 (lab supply) | - | |
| Power, energy per operation | n/a: no operation measured here | - | |
| Per day | n/a | - | |
| Temperature die / board (start / peak / end) | n/a: no load | - | |
| Memory BL616 / K230 (start / min / end), leak | n/a: no repeated operation (one window, one hang) | - | |
| Image (sizes) | setup test bed 1284 KB, full 1086 KB | slot sizes | |
| Compile flags | BL616 -Os -g (SDK default), no LTO | smallest code (rule) | |
| Features over the minimal image | `lp_bod`, `lp_wdg` (test bed commands), the sleep counters' watchdog fields | - | |
| Optimised build (-O3, LTO, -march) | n/a | - | |
