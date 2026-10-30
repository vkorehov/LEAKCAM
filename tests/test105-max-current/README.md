# test105: maximum current - can the battery carry everything at once?

Status: **partial** (2026-10-25): handover runs 1-2 done - USB -> cell under every camera-free load (1.06 A at 5 V),
no power fault, K230 at 60.2 C afterwards; "no K230 reset on the cell" not proven; the BL616's USB console left the bus
at the unplug (open); runs 1-4 of the plan not done.

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

## Result

- USB input with every camera-free load on (both LED chains 100 %, Wi-Fi TCP upload ~21-29 Mbit/s, DDR + CPU
  `memtest`): **1.05-1.07 A at 5 V** (~5.3 W; ~1.25-1.3 A from a 4.1 V cell) (handover runs 1-2, 2026-10-25).
- USB unplugged under that load: **no power fault** - K230 rails stayed on, LEDs lit at the same brightness (user),
  K230 answering afterwards; cell cold by hand (handover runs 1-2, 2026-10-25).
- K230 die **60.2 C** after ~5 min of full load (~3 min of it on the cell) (handover run 2, 2026-10-25).
- memtest must leave memory: with 109 of 128 MB taken a program failed to load ("ELF loading failed"); memtest with
  24 MB reserved runs clean (0 errors, 89 MB) (handover runs 1-2, 2026-10-25).
- Plugged back in while the K230 still runs: **no charging** until `pt_off` - bug-002 (the rails back-feed VUSB to
  ~2.2 V, a USB-C source waits for < 0.8 V before it turns VBUS on) (handover run 2, 2026-10-25).
- After the run the cell recharged at 0.9 A, tapering to 0.6 A within minutes (~50 mAh used, ~3 %) (handover run 1,
  2026-10-25).

Open:
- **No K230 reset while on the cell not proven** - a rebooted K230 also answers the probe, and lit LEDs prove nothing
  (an unconfigured PWM channel lights at full, test009).
- **Why the BL616's USB console (PR1) left the bus** when USB-C 1 and USB-C 2 were unplugged under ~1.3 A (the BL616
  kept running, rails on; it came back with USB-C 1). Next run: USB-C 2 out from the start, unplug only USB-C 1
  (console survives -> USB-C 2 / D3 involved; drops -> the unplug transient on USB-C 1, e.g. a dip at the BL616's D+
  pull-up); if it drops again, log on the board instead.
- Runs 1-4 of the plan (per-step currents, 10 min full load, cold start into full load, low cell) and cameras.

## Metrics (skill leakcam-test-metrics)

**MISSING**: the full block of skill leakcam-test-metrics (the handover runs recorded only the figures in Result).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median / max of 5) | **MISSING** | target (test022) | longest step |
| This part's boot share | **MISSING** | - | driver / mount / probe |
| Test run time | **MISSING** | - | setup separately |
| Speed / rate against the top | **MISSING** | SPEEDS.txt | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 VBUS meter |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | probe points named |
| Power, energy per operation | **MISSING** | - |  |
| Per day | **MISSING** | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; mem_leak.py over >= 8 cycles |
| Image (sizes) | **MISSING** | minimal image (rule) | the image this run used |
| Compile flags | **MISSING** | smallest code (rule) |  |
| Features over the minimal image | **MISSING** | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays |  |

## Boot time

**MISSING**: the boot time of this test's image and its share.

## Battery energy

**MISSING**: current -> power -> energy per operation and per day.
