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

### Re-run with currents, 2026-10-25 12:11 (`led_steps.py 15`, battery, meter in the cell lead, mA range)

K230 at msh, BL616 test firmware, both chains stepped on the K230 PWM (25 kHz), 15 s per step; die (`chip_temp`) and
board (AHT20) after each step. Meter: 600 mA range (shunt + fuse drop the board's supply at ~0.5 A).

| Step | Battery | Above idle | Die / board C |
|---|---|---|---|
| K230 idle, LEDs off | 221 mA | - | 36.4 / 29.6 |
| white 10 / 25 / 50 % | 257 / 312 / 411 mA | +36 / +91 / +190 | 39.8 / 30.6-32.4 |
| white 75 / 100 % | 515 / 519 mA | +294 / **+298** | 42.2-43.1 / 33.5-34.3 |
| white off | 221 mA | 0 | 44.9 / 34.6 |
| IR 10 / 25 / 50 / 75 % | 257 / 313 / 413 / 511 mA | the same as white | 45.2-46.7 / 34.9-36.3 |
| IR 100 % or both 100 % (one reading missing) | 500 mA | | 47.3-48.2 / 37.0-38.1 |
| both off | 223 mA | | |

- Both chains take the same input power per step (designed: white 67 mA x ~22 V = IR 133 mA x ~11 V = ~1.47 W).
- **75 -> 100 % adds nothing**: the TPS61161 switch current limit (the knee LED_STRIPS.txt designs for) is reached at
  ~70 % duty. +0.30 A at 100 % is ~2/3 of the +0.42 A the full target needs from 4.1 V. The meter's mA range lowers the
  board supply at 0.5 A, which moves the knee down; repeat 75 / 100 % and both on a 10 A range
  (`led_steps.py 15 top`) to tell the meter from the board.
- Heat over the 4 min run (K230 idle, LEDs stepping): die 34.6 -> 48.2 C, board 28.7 -> 38.1 C.
- Obstacle: my run call was interrupted at its end, before `pt_off`: the K230 stayed on until switched off by hand.

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
