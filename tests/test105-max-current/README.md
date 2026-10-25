# test105: maximum current - can the battery carry everything at once?

Status: **planned** (2026-10-25; user, after test009: the cell carried K230 + both LED chains, ~1.15 A, on a USB
unplug with no brownout and no visible dimming).

The worst case the cell and the power path must survive: every consumer on the board busy at the same time, on a
full and on a low cell. Rev 1 has no hardware discharge cutoff (bug-004): a sag below the rails' minimum would reset
the K230 or the BL616 mid-write. This test finds the peak current, the sag it causes, the heat it makes, and the
lowest rest voltage at which the full load still runs without a brownout.

## Setup

- Meter in the cell's + lead, **10 A range, locked** (the 600 mA range's shunt dropped the supply at 0.5 A and hid
  the LED knee, test009), jumper across it while the range or jack changes; every USB power cable out (PR1 only, the
  consoles: `/dev/ttyACM0` BL616, `/dev/ttyACM1` K230).
- VBAT at C15.1 on a second meter (minimum hold) or a scope for the dips (the LED soft start, the Wi-Fi TX bursts).
- Full cell (charged, D1 off, ~4.17 V at rest) for runs 1-3; a low cell (~3.5 V and ~3.3 V at rest, drained by
  running run 2 on battery) for run 4.
- Board in the open (no enclosure) for the first runs; then once in the closed enclosure for the heat.

## Needs (build features and dependencies)

K230 image with the test tools: `leakcam_cpubench` (test008), `memtest` (test025), `venctest` (test027), the change
net loop (test013), NAND fill (test011), `iperf` (test100), PWM shell commands (test009), `chip_temp`; BL616 test
firmware with the bridge (`pt_wifi bridge`) and `pt_session`-style supervision; the LAN PC's `pc_tcp_bench.py`
(test100). Cameras (capture + ISP) once they arrive.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

Each load is started one by one on top of the previous (the current added by each is recorded), then all together.

| # | Load | Tool | Expected |
|---|---|---|---|
| 1 | K230 idle at msh | - | 0.22 A @ 5 V (test009) |
| 2 | + CPU: both cores busy | `leakcam_cpubench` in a loop (2 instances) | |
| 3 | + DDR: memtest | `memtest` | |
| 4 | + KPU: change net back to back | test013 loop | |
| 5 | + VPU: H.265 2 x 15 fps (then the encoder's top, ~83 fps) | `venctest` | |
| 6 | + NAND: continuous writes | test011 fill | |
| 7 | + Wi-Fi: TCP upload to the PC | `iperf -c`, BL616 bridge | ~27 Mbit/s |
| 8 | + both LED chains at 100 % | `pwm_set` | +0.73 A @ 5 V (test009) |
| 9 | + cameras streaming | (after the cameras arrive) | |

Runs:
1. **Steps 1-8 on a full cell**, 30 s per step: battery current per step, VBAT minimum, resets (K230 console, BL616
   `evlog`), die / board temperature.
2. **All loads for 10 min** on a full cell: the current, VBAT and heat over time (does the K230 throttle? does the
   board reach a limit?).
3. **Cold start into full load**: rails on with the cell at rest, all loads started at once (inrush + LED soft start
   + Wi-Fi join at the same moment).
4. **Low cell**: run 1's step 8 (everything on) at 3.5 V and then 3.3 V rest: the sag under the full load against the
   3.10 V cutoff (test038) and the rails' minimum (TPS62823 >= 2.4 V, TPS63802 >= 1.3 V); the rest voltage at which
   the cut first fires under full load = the usable end of the cell.

## Pass

No brownout or reset in any run on a full cell; VBAT under the full load stays above the 3.10 V cutoff down to a
3.5 V rest voltage; no component past its limit (die temperature, LED driver, cell discharge rating ~1C continuous /
2C bursts); every figure recorded.

## Results

Not run yet.

## Metrics (skill leakcam-test-metrics)

**MISSING** until the first run.

## Obstacles

None yet.
