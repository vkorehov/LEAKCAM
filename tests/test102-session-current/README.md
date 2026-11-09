# test102: K230 session current

Status: **deferred** (2026-11-08, user: "defer to later"): to be rewritten on the product pair (the energy of one leak check; test132 has its timing: the K230 on ~1.6 s) and read with the cell out or full; its run.py is the old test firmware's (`pt_session`); a product version would read the USB-C 1 meter, which also counts the cell's charging current (the cell not full), so no reading is the board's own.

Battery life = sleep current x time + check-in energy x check-ins + leak-check energy x leak checks (DESIGN.md 5.3.12:
24 check-ins + 1 leak check a day, ~2.85 mAh/day estimate). The K230 rails' current through a leak check (READY, WAKE,
the job: capture, change net, `DONE`) and, on a leak, the encoder and Wi-Fi.

## Setup

**At USB (the way it is done):** a multimeter in the VBUS wire of the **USB-C 1** cable, the only cable carrying power
(USB-C 2's VBUS cut, 2026-10-21; PR1 carries data only): one cable powers the board and flashes the K230
([usb_meter.png](usb_meter.png) shows the meter in a USB cable's VBUS). Consoles: both on PR1 (`/dev/ttyACM0` BL616,
`/dev/ttyACM1` K230, tests/test039); USB-C 2 is not needed. The cell must be charged (D1 off): then the USB current is
the board's own draw. Board power = that current x VBAT (C15 pad 1), not x 5 V (the linear charger's drop is not
paid on battery). A / 10 A input, 0.001 A. Rules (2026-10-21):
- **10 A jack and range, locked** (no autorange). The mA range's shunt + fuse (2-5 ohm) dropped VBUS 0.4-1 V at
  200 mA and reset the board on current steps; autorange switching opens the path briefly.
- **Meter in the red VBUS wire**, GND joined straight through: a meter in GND offsets the board's ground against the
  host and every USB device on the board (PR1, K230) fails.
- **Check the mA fuse** of a borrowed meter: with a burned fuse it reads nothing useful.
- **Only one cable may carry power**: both USB-C ports feed VUSB, so with two plugged in the meter reads a share.
- **Meter in after the port is up**: plug USB-C 1 with the meter leads shorted, wait until the board runs (D1 solid
  if charging), then open the short. With the meter in line at plug-in, the voltage sags at the port's switch-on
  and the charger's ~1 A start, and the charger only blinks D1 (no VIN-DPM in ILIM mode, bugs/v2.txt 9).
- "LEAd" on the GD118B = the red lead's jack does not match the function; it has auto power-off (APO) - disable it
  (key at switch-on, see the manual) for long runs.

**Sleep current:** test031's reading (35 uA on the battery, 2026-10-25); no battery measurements here (user,
2026-10-30).

![USB meter hookup](usb_meter.png)

## Needs (build features and dependencies)

**MISSING**: the kernel options / drivers, SDK components, libraries and programs this test uses (the union over all
tests is what the minimal image keeps).

## Steps

1. Rails off (BL616 only): sleep-level current (measured in test031: 35 uA on the battery, 2026-10-25).
2. `pt_on` held in reset, booting, `/app/leakcam` idle after READY (the USB relay only), the job (cameras simulated
   until they arrive; then capture with the LEDs at 100 %, white and IR, and the change net on the KPU), the encoder
   (test027) for a clip.
3. Integrate over a leak check (test101: READY -> job -> `DONE` -> rails off) for the energy per leak check.

## Pass

Recorded figures; energy per leak check against the ~0.3 mAh estimate (DESIGN.md 5.3.12) and the days a 1000 mAh
cell allows.

## Result

Not run on the product program yet.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | **MISSING**: deferred 2026-11-08 (read with the cell out or full: on USB the meter includes the charging current) | target (test022) | longest step: |
| This part's boot share | n/a: the whole session is the subject (test101 has the steps) (2026-10-30) | - | |
| Test run time | 218 s automated (run.py, 2026-10-30), no setup | - | setup (flashing, plugging, waiting) separately |
| Speed / rate (none for this test) | **MISSING**: deferred 2026-11-08 (read with the cell out or full: on USB the meter includes the charging current) | - | % of the top |
| Current idle / during (min / avg / peak) | K230 running with no camera and no server: 228 mA (the first product's agent session, 2026-10-30); the job with /app/leakcam **MISSING**; rails off, BL616 awake 61 mA (test006); K230 rails on, held in reset 87 mA (test021); K230 at msh idle 220 mA (test006); KPU loop 220 mA (test013); encoder 2 x 15 fps 235 mA (test027: +15 mA); both LED chains 100 % 950 mA (test009: +0.73 A) (2026-10-30) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | n/a: on USB power; no battery measurements (user, 2026-10-30) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING**: deferred 2026-11-08 (read with the cell out or full: on USB the meter includes the charging current): one leak check with /app/leakcam (estimate ~0.3 mAh, DESIGN.md 5.3.12; the 2026-10-30 agent-session figure superseded: HISTORY.md) | - | scripts/energy.py |
| Per day | **MISSING**: deferred 2026-11-08 (read with the cell out or full: on USB the meter includes the charging current): one leak check a day (the server's `leakcheck`, default 86400 s) plus one per wet probe; budget 24 check-ins + 1 leak check ~2.85 mAh/day (estimate, DESIGN.md 5.3.12) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die n/a: K230 off; board 37.3 / 37.3 / 37.3 C (AHT20) (2026-10-30) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 131124 / min 105196 / 131124 B, blocks 106 -> 106, deleted 0; leak: n/a, 1 snapshots (mem_leak.py needs >= 8 cycles) (2026-10-30) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | BL616 test firmware 1309616 B (2026-10-30, on the board at the run) | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -Os -g (SDK Kconfig default), no LTO (2026-10-30) | smallest code (rule) | |
| Features over the minimal image | **MISSING**: deferred 2026-11-08 (read with the cell out or full: on USB the meter includes the charging current): this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

Boot energy per leak check from test022's timing x its current.

## Battery energy

This test is the energy measurement of a leak check (a K230 session); its figures go into ENERGY.txt.
