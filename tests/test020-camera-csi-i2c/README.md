# test020: Camera interfaces: CSI frame rate, I2C clock

Status: **pass for CSI** (2026-11-11 22:40 UTC+2): both cameras 10 min at once at the product rate (15 fps H.265),
identical frame counts, no drop; top 32.7 fps per camera with both. I2C clock (step 1) not measured yet.

After the cameras arrive (OV5647 on CSI0 / CSI2, I2C0 / I2C4).

## Setup

**MISSING**: hookup, firmware / image, and the probes named in Metrics.

## Needs (build features and dependencies)

I2C0/I2C4, CSI0/CSI2, VICAP, OV5647 sensor driver (CONFIG_MPP_ENABLE_SENSOR_OV5647), ISP files in /bin.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. I2C: SCL frequency (from the sensor's register read time), 100 vs 400 kHz, error-free register dumps.
2. CSI: both sensors at 1280x960 binned, 45 fps (DESIGN.md): frames per second delivered, dropped frames, both at
   once; lanes and data rate.
3. Capture to encoder (test027 with real frames): the pipeline's frame rate.

## Pass

Both cameras at the design rate with no dropped frames for 10 min; I2C clean at the configured clock.

## Result

Run with `tests/test030-camera-capture/stream.py <s> <fps>` (`leakcam_stream -f`: sensor -> VICAP -> ISP -> VENC
bound in hardware, both cameras, two standard-lens OV5647 on J4/CSI0 and J5/CSI2, BL616 bl616_powertest, USB power):
- **10 min, both cameras, 15 fps, H.265 CBR 500 kbit/s** (2026-11-11 22:30-22:40): cam0 and cam1 **8819 frames each**
  in 600 s = 14.68 fps, 59 IDRs each (GOP 10 s), 487 kbit/s each; longest gap between two frames 119 / 121 ms
  (one frame time late at most). Equal counts on both = no drops (drops would differ); 14.68 instead of 15 = the
  sensor's real rate (~44 fps / 3). The first 4 MB of each decodes on the PC (ffprobe 1151 / 1050 frames;
  cam1: one "missing picture in access unit with size 86" warning at the cut). **pass**.
- **Top rate, both at once, 10 s** (22:23-22:24): `-r 30` -> 29.0 / 28.8 fps, `-r 45` -> 32.7 / 32.7 fps: the
  pipeline's top with two cameras (one ISP shared in offline mode, ~65 frames/s); 15 fps has 2x margin.
- I2C: both sensors answer (chip ID 0x5647 on i2c0 and i2c4, register table read back, test030); the SCL clock and
  error-free dumps at 100 / 400 kHz: to do.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | **MISSING** | target (test022) | longest step: |
| This part's boot share | **MISSING** | - | |
| Test run time | 606 s run + 66 s pulling 2 x 4 MB over the console (2026-11-11) | - | setup (flashing, plugging, waiting) separately |
| CSI lane rate, I2C clock | frames: 14.68 fps per camera at 15 requested, top 32.7 fps with both (73 % of the 45 fps sensor mode); lane rate and I2C clock **MISSING** | OV5647 2-lane, I2C 400 kHz | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING** | - | scripts/energy.py |
| Per day | **MISSING** | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 41.0 C before, 52.4 C right after 10 min of both cameras + encoder (board not read) (2026-11-11) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | K230 heap available 7020032 B before, 7306688 B after the run (program ended); leak: n/a, one run | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | **MISSING** | minimal image (rule) | the image this run used |
| Compile flags | **MISSING** | smallest code (rule) | |
| Features over the minimal image | **MISSING**: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

Sensor probe and init time at boot, both sensors (without cameras the probes fail: their timeout is in today's boot).

## Battery energy

Battery current with one and two sensors streaming (vs K230 idle); energy per second of capture and per still.
