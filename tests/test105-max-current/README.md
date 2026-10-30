# test105: maximum current - can the battery carry everything at once?

Status: **pass on USB** (2026-10-30): per-load currents recorded; all camera-free loads 10 min with 0 K230 resets, die
68 C at the end; with the LEDs on the USB meter's series resistance capped the input at ~0.57-0.60 A and the cell
carried the rest (board ~1.13 A; 1.05 A from USB through a 10 A-range meter on 2026-10-25). Cell-only
runs: n/a (no battery measurements, user 2026-10-30).

The worst case the cell and the power path must survive: every consumer on the board busy at the same time, on a
full and on a low cell. Rev 1 has no hardware discharge cutoff (bug-004): a sag below the rails' minimum would reset
the K230 or the BL616 mid-write. This test finds the peak current, the sag it causes, the heat it makes, and the
lowest rest voltage at which the full load still runs without a brownout.

## Setup

- USB-C 1 meter (5 V, VBUS), cell full (input back at the BL616-only level with the K230 off: D1 does not light for a
  top-up charge); BL616 test firmware; K230 image with `leakcam_kputest`, `venctest`, `memtest`, `iperf`.
- The PC's `pc_tcp_bench.py sink` on 192.168.1.131:5001; the Wi-Fi bridge started with the rails up and the K230 in
  reset (`Run.boot(after_on=...)`), then `bl616_rescan` on the K230 binds the card.
- Board in the open, room temperature.

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
| 2 | + KPU: change net back to back | `leakcam_kputest 2000000 &` | |
| 3 | + VPU: H.265 2 x 15 fps | `venctest 2 <s> 15 &` | |
| 4 | + NAND: writes | `cp` / `rm` on /sdcard, repeated by the runner | |
| 5 | + Wi-Fi: TCP upload to the PC | `iperf -c`, BL616 bridge | |
| 6 | + CPU + DDR: memtest on the memory left | `memtest 0 24 &` | |
| 7 | + both LED chains at 100 % | `pwm_set` / `pwm_enable` | +0.73 A @ 5 V (test009) |
| 8 | + cameras streaming | (after the cameras arrive) | |

memtest goes last: it takes all the MMZ and system RAM left, and the KPU and the encoder started after it fail
(mmz alloc). Every load's start-up output is checked; one that did not start stops the run before its reading.

Runs:
1. **Steps 1-8 on USB power** (USB-C 1 meter), 30 s per step: current per step, resets (K230 console, BL616
   `evlog`), die / board temperature.
2. **All loads for 10 min** on USB: the current and heat over time (does the K230 throttle? does the
   board reach a limit?).
3. Cold start into full load and the low-cell runs on the cell: n/a, no battery measurements (user, 2026-10-30); the cut-off under load is test038's
   (lab supply).

## Pass

No brownout or reset in any run on a full cell; VBAT under the full load stays above the 3.10 V cutoff down to a
3.5 V rest voltage; no component past its limit (die temperature, LED driver, cell discharge rating ~1C continuous /
2C bursts); every figure recorded.

## Result

Per load, USB-C 1 at 5 V, each on top of the ones before (2026-10-30, the user at the meter):

| Load | Input average | Peak |
|---|---|---|
| K230 idle at msh, agent running | 220 mA | |
| + KPU change net | 242 mA (+22) | |
| + encoder 2 x 15 fps | 285 mA (+43) | |
| + NAND writes | 275 mA | |
| + Wi-Fi TCP upload | 390 mA (+115) | 411 mA |
| + memtest (CPU + DDR) | 400 mA | |
| + both LED chains 100 % | 563 mA, rising to 600 mA over 10 min | |

- **USB input capped at ~0.57-0.60 A** with the LEDs on: VBAT fell from 4.30 to 4.03 V at once (the charger left the
  supply rail at the cell), so the cell supplies the rest. Board total ~1.13 A (0.40 A + 0.73 A for the LED chains,
  test009; the BQ24072 is linear, currents add), cell share ~0.56 A (derived, no battery measurements). ILIM is set
  for 1.0 A (R5 1.5 k): the cap is the measurement: the USB meter's shunt and leads (user, 2026-10-30) drop VBUS
  until the charger's input voltage regulation cuts the current. The 10 A-range meter of 2026-10-25 passed
  1.05-1.07 A for the same load. The readings up to 0.40 A are below the cap and unaffected.
- The input rose 563 -> 600 mA over the 10 min as the board heated (board 45 -> 55 C, die 60 -> 68 C).
- **10 min full load: 0 K230 resets**, NAND writes continuing; VBAT 3.99 -> 3.93 V.
- Memory over the 10 min (131 samples): BL616 free 122720 B flat, K230 free 5.96 MB flat (+32 B/sample): no leak.
- After the run: 4.15-4.17 V at idle, above the recharge threshold (~4.1 V), so no charge until it fell below; the
  top-up then ran at ~0.44 A with D1 off (BQ2407x: /CHG lights for the first cycle after plug-in only).
- 2026-10-25 handover runs (USB unplugged under full load): no power fault, K230 rails stayed on; plugged back in
  while the K230 runs: no charging until `pt_off` (bug-002).

Open:
- Cell-only runs (cold start into full load, low cell): n/a, no battery measurements (user, 2026-10-30).
- One VBAT sample of 3.47 V at 380 s of the full load (neighbours 3.99 / 4.02 V, no reset): ADC glitch or a real
  dip; no scope on this bench (user, 2026-10-30).
- Why the BL616's USB console left the bus at the 2026-10-25 unplug (HISTORY.md).
- Cameras.

## Metrics (skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median / max of 5) | pt_run -> msh 0.56 s (1 boot with the bridge: SPL 0.16, OpenSBI 0.27, RT-Smart 0.28, mounted 0.55); boot timing: test022 (2026-10-30) | target (test022) | longest step: the mounts |
| This part's boot share | n/a: loads after the boot (2026-10-30) | - | driver / mount / probe |
| Test run time | 13 min automated (7 steps + 10 min full load, run.py, 2026-10-30); the meter readings by the user | - | setup separately |
| Speed / rate against the top | n/a: a load test; the encoder held 2 x 15 fps under every load, Wi-Fi rate: test100 (2026-10-30) | SPEEDS.txt | % of the top |
| Current idle / during (min / avg / peak) | idle 0.220; + KPU 0.242; + encoder 0.285; + NAND 0.275; + Wi-Fi upload 0.390 (peak 0.411); + memtest 0.400; + LEDs 0.563 -> 0.600 A input, capped by the meter's resistance (board ~1.13 A) (USB-C 1, 5 V, 2026-10-30) | floor (test031) | USB-C 1 VBUS meter |
| Voltage under load (min) | VBAT 3.93-4.02 V over the 10 min full load (the cell under the supplement); one sample 3.47 V at 380 s between 3.99 and 4.02 V, 0 resets: unexplained, ADC or real dip (2026-10-30) | cutoff 3.10 V (bug-004) | K230 ADC ch 1 |
| Power, energy per operation | full load: USB 0.60 A x 5 V = 3.0 W + cell ~0.56 A x 3.95 V = ~2.2 W, ~5.2 W board (2026-10-30) | - |  |
| Per day | n/a: the worst case, not a daily state (ENERGY.txt carries the session) (2026-10-30) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 48.2 / 68.6 / 67.7 C; board 37.6 / 55.2 / 54.9 C (AHT20), open board, room temperature (2026-10-30) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | 10 min full load, 131 samples: BL616 free 122720 B flat, blocks 153 -> 153; K230 free 5.96 MB flat (+32 B/sample): no leak (mem_leak.py) (2026-10-30) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free` |
| Image (sizes) | K230 kdimg 45441024 B, BL616 test firmware 1313712 B (2026-10-30, on the board at the run) | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -Os -g (SDK Kconfig default), no LTO (2026-10-30) | smallest code (rule) |  |
| Features over the minimal image | n/a: the test tools image (kputest, venctest, memtest, iperf), not a product feature (2026-10-30) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays |  |

## Boot time

pt_run -> msh 0.56 s on this image with the bridge up; the loads start after the boot (2026-10-30).

## Battery energy

Full load ~5.2 W (USB 3.0 W + cell ~2.2 W), board ~1.13 A; on the cell alone ~1.13 A, a ~1C load for a ~1 Ah
cell. The worst case, not a daily state (2026-10-30).
