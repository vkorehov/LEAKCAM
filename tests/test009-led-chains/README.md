# test009: LED chains (white U14/J1, IR U15/J2)

Status: **pass** (2026-10-16/18).

Wiring: [led_wiring.png](../../tools/power-bringup/led_wiring.png); design: [LED_STRIPS.txt](../../LED_STRIPS.txt).

## Setup

**MISSING**; the setup is in the introduction above and in the Results.

## Needs (build features and dependencies)

K230 PWM (pwm0/pwm1), msh pwm commands.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

**MISSING**; the steps are the rows of the Results tables.

## Pass

**MISSING**; every Results row carries its pass / fail.

## Results

### Strips on a bench supply, 2026-10-16

Each whole chain (8 LEDs through all four strips) from a bench supply, raised until the LEDs light.

| Chain | LEDs | Lights from | Per LED | Running point |
|---|---|---|---|---|
| White (J1) | 8 | 19.6 V | 2.45 V | ~22 V at 64 mA |
| IR (J2) | 8 | 10.0 V | 1.25 V | ~11 V at 133 mA |

**Pass.** All 16 LEDs light, every strip has + on pin 2. IR was first wired reversed (from an earlier version of
the wiring page); swapped at J2. One IR pair saw up to 14 V in reverse (~7 V per LED, rated 5 V); all light now.

### Driven by the K230, 2026-10-18

RT-Smart shell: `pwm_disable pwm <other>`, `pwm_set pwm <ch> 40000 <pulse ns>`, `pwm_enable pwm <ch>` (channel 1 =
white, channel 0 = IR; 25 kHz; I = duty x 0.2 V / R_FB: white 66.7 mA, IR 133 mA at 100 %; LEDs rated 350 mA / 1 A).
Probe sheets: [led_drivers.png](led_drivers.png), [r66_white_return.png](r66_white_return.png).

| Step | Result | |
|---|---|---|
| White 10 % / 50 % | nothing; J1 + at 37 V (open chain); J1 pin 2 not joined to its pad / the R66 track. Resoldered | fixed |
| White 10 % (~7 mA), 50 % (~33 mA) | all lit, brighter at 50 % | pass |
| IR 10 % (~13 mA), 50 % (~67 mA) | lit (phone camera), brighter at 50 % | pass |
| One channel enabled, the other never configured | the other chain lit at full current | fixed (firmware) |

**Pass.** Brightness at 2-3 m: with the cameras.

### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

## Metrics (**MISSING** on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - LED boost outputs J1.1 / J2.1 and LED_CTL2 (led_drivers.png), VBAT C15.1.

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | **MISSING** (2026-10-21); fill on a re-run | test022 | longest step: |
| This part's boot share | **MISSING** (2026-10-21); fill on a re-run | - | |
| Test run time | **MISSING** (2026-10-21); fill on a re-run | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | **MISSING** (2026-10-21); fill on a re-run | SPEEDS.txt | % of the top |
| Current floor / typical / peak | **MISSING** (2026-10-21); fill on a re-run | floor (test031) | at the probe above |
| Voltage under load (min) | **MISSING** (2026-10-21); fill on a re-run | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING** (2026-10-21); fill on a re-run | - | scripts/energy.py |
| Per day | **MISSING** (2026-10-21); fill on a re-run | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** (2026-10-21); fill on a re-run | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | **MISSING**: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | **MISSING**: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

n/a for boot (the LED drivers are idle until used); PWM init time is in test022's driver init if visible.

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: battery current with white and IR at 25/50/100 % duty; mA per chain at full on (the clip lighting cost per second).

## Obstacles

- **White never lit; J1 + at 37 V** -> open chain; LEDs came on while an ohmmeter bridged J1 pin 2 to GND -> J1 pin 2
  (white -) not joined to its pad / the 2 mm bottom track to R66 -> resoldered.
- **The "wrong" chain lit** -> PWM0..2 share one counter; a channel's output is high while the counter is at or above
  its compare register, 0 after reset -> shell: disable the other channel first; firmware `led_set()` disables the
  off channels before enabling.
