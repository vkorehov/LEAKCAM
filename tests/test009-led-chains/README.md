# test009: LED chains (white U14/J1, IR U15/J2)

Status: **pass** (2026-10-25): both chains light and reach full current at 100 % duty on the K230 PWM (white +0.34 A,
IR +0.38 A, both +0.73 A at 5 V USB input); the battery carries both chains + the K230 without a brownout.

Wiring: [led_wiring.png](../../tools/power-bringup/led_wiring.png); design: [LED_STRIPS.txt](../../LED_STRIPS.txt).

## Setup

K230 at msh, BL616 test firmware; RT-Smart shell: `pwm_disable pwm <other>`, `pwm_set pwm <ch> 40000 <pulse ns>`,
`pwm_enable pwm <ch>` (channel 1 = white, channel 0 = IR; 25 kHz; I = duty x 0.2 V / R_FB: white 66.7 mA, IR 133 mA
at 100 %; LEDs rated 350 mA / 1 A). Probe sheets: [led_drivers.png](led_drivers.png),
[r66_white_return.png](r66_white_return.png). Steps by `led_steps.py 15 [top]` and run.py; die (`chip_temp`) and
board (AHT20) after each step.

## Needs (build features and dependencies)

K230 PWM (pwm0/pwm1), msh pwm commands.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

**MISSING**; run.py steps white and IR 25 / 50 / 100 %, then both 100 % for 2 min.

## Pass

**MISSING**; every Result line carries its pass / fail.

## Result

- All 16 LEDs light, every strip has + on pin 2 (2026-10-16, bench supply). Lights from: white chain 19.6 V
  (2.45 V per LED, ~22 V at 64 mA), IR chain 10.0 V (1.25 V per LED, ~11 V at 133 mA).
- Both chains lit from the K230 PWM, brighter with duty; white and IR stepped 25 / 50 / 100 %, both 100 % for 2 min:
  pass (2026-10-25, run.py).
- Full current at 100 % (USB-C 1 input at 5 V, 10 A range, cell full): idle 0.22 A; white 100 % 0.56 A (+0.34 A,
  1.7 W in), IR 100 % 0.60 A (+0.38 A, 1.9 W), both 100 % 0.95 A (+0.73 A, 3.65 W); 75 % -> +0.28 A (1.4 W) each
  (2026-10-25 12:24). Each chain needs ~1.7 W in at its full target (1.47 W out at ~86 %): both reach full current on
  the USB-fed system rail (~4.4-5 V); on a full cell LED_STRIPS.txt puts them at the knee.
- Both chains take the same input power per step (designed: white 67 mA x ~22 V = IR 133 mA x ~11 V = ~1.47 W)
  (2026-10-25 12:11).
- USB unplugged during "both 100 %": the battery took the whole load (K230 + both chains, ~1.15 A from the cell) with
  no brownout; the K230 kept running (2026-10-25 12:24). User: the same brightness on the battery. Both chains + the
  K230 from the battery: ~1.1-1.2 A during a lit shot (plan ~1-1.5 A); BQ24072 battery FET rated 4.5 A, the 1000 mAh
  cell ~1C continuous / 2C bursts: a lit shot (< 1 s) is within it.
- PWM0..2 share one counter: firmware `led_set()` disables the off channels before enabling (2026-10-18).
- Heat, both 100 % for 2 min: die 41.6 -> 48.2 C, board 33.7 -> 41.2 C (2026-10-25, run.py).

Open:
- Brightness at 2-3 m: with the cameras (2026-10-18).

## Metrics (**MISSING** on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - LED boost outputs J1.1 / J2.1 and LED_CTL2 (led_drivers.png), VBAT C15.1.

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | pt_run -> msh 0.49 / 0.49 s (median / max of 1; SPL 0.16, OpenSBI 0.24, RT-Smart 0.25, mounted 0.48, msh 0.49); longest step to mounted 0.23 s (2026-10-25, run.py) | test022 | longest step: |
| This part's boot share | n/a: the LEDs start in the program, not in the boot (2026-10-25) | - | |
| Test run time | 227 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | n/a: light output by design (LED_STRIPS.txt), brightness not measured | SPEEDS.txt | % of the top |
| Current floor / typical / peak | 0.22 A idle / white 100 % 0.56 A / IR 100 % 0.60 A / both 100 % 0.95 A at 5 V (10 A meter, 2026-10-25 12:24, Result) | floor (test031) | at the probe above |
| Voltage under load (min) | VBAT 4.00 V min (K230 ADC ch 1, test023 calibration, 20 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | both chains 100 %: 0.95 A x 4.35 V = 4.1 W; per 20 s of light 83 J, 5.3 mAh (0.73 A above idle: 4.1 mAh) (2026-10-25, from recorded readings) | - | scripts/energy.py |
| Per day | light only on alarms (rare, no daily budget line): 1 alarm a day with 20 s of light = 5.3 mAh/day, 8 % of 2000 mAh per month (2026-10-25, from recorded readings) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 41.6 / 48.2 / 48.2 C; board 33.7 / 41.2 / 41.2 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 206440 / min 204396 / 206440 B, blocks 16 -> 16, deleted 0; K230 heap available 7025152 / 7025152 / 7312064 B; BL616 no leak (mem_leak.py, 20 snapshots); K230 no leak (mem_leak.py, 20 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | n/a: the PWM driver is part of the product floor (the LEDs light every alarm) (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (TPS61161 LED drivers): compiler flags do not change the result (2026-10-25) | the faster one stays | |

## Boot time

n/a for boot (the LED drivers are idle until used); PWM init time is in test022's driver init if visible.

## Battery energy

Measured on the USB input at 5 V, cell full (2026-10-25 12:24): white 100 % +0.34 A, IR 100 % +0.38 A, both 100 %
+0.73 A above the K230 idle; both chains at 100 %: per 20 s of light 83 J, 5.3 mAh (Metrics).
