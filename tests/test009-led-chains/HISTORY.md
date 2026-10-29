# test009: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-10-25 (run.py, 227 s): pass

white and IR stepped 25 / 50 / 100 %, both 100 % for 2 min: die 41.6 -> 48.2 C.

### The knee on a 10 A range, 2026-10-25 12:24 (`led_steps.py 15 top`, meter in the USB-C 1 charge line, cell full)

Cell charged first (charge current tapered 0.24 -> 0.06 A, D1 off), so the USB input is the board's load alone, at 5 V.

| Step | USB input | Above idle | Input power above idle | Die / board C |
|---|---|---|---|---|
| K230 idle, LEDs off | 0.22 A | - | - | 38 / 33 |
| white 75 % | 0.50 A | +0.28 A | 1.4 W | |
| white 100 % | 0.56 A | +0.34 A | **1.7 W** | 42.5 / 34.6 |
| white off | 0.22 A | 0 | | 44.6 / 35.2 |
| IR 75 % | 0.50 A | +0.28 A | 1.4 W | 45.5 / 36.2 |
| IR 100 % | 0.60 A | +0.38 A | **1.9 W** | 46.1 / 37.4 |
| both 100 % | 0.95 A | +0.73 A | 3.65 W | 47.9 / 39.1 |

- **100 % beats 75 % here**: the flat top of the first run was the 600 mA range's drop on the supply, not the board.
- Each chain needs ~1.7 W in at its full target (1.47 W out at ~86 %): white draws exactly that, IR a little more, so
  both reach full current on the USB-fed system rail (~4.4-5 V); on a full cell LED_STRIPS.txt puts them at the knee.
- Both chains + the K230 from the battery: ~1.1-1.2 A during a lit shot (plan ~1-1.5 A).
- **USB unplugged during "both 100 %": the battery took the whole load (K230 + both chains) with no brownout**; the
  K230 kept running. The runner died on the console's I/O error at that moment (the BL616's USB re-enumerated), so
  "both off" was never sent: switched off by hand (`pt_off`). Fixed: the runner switches the LEDs and the rails off
  in a `finally`. User: the same brightness on the battery, good enough for the battery test.
- The cell gave ~1.15 A (0.95 A x 5 V / ~4.1 V) without a sag the LEDs showed; BQ24072 battery FET rated 4.5 A,
  the 1000 mAh cell ~1C continuous / 2C bursts: a lit shot (< 1 s) is within it.
- Heat over 2 min: die 38 -> 48 C, board 33 -> 39 C.

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

### Strips on a bench supply, 2026-10-16

Each whole chain (8 LEDs through all four strips) from a bench supply, raised until the LEDs light.

| Chain | LEDs | Lights from | Per LED | Running point |
|---|---|---|---|---|
| White (J1) | 8 | 19.6 V | 2.45 V | ~22 V at 64 mA |
| IR (J2) | 8 | 10.0 V | 1.25 V | ~11 V at 133 mA |

**Pass.** All 16 LEDs light, every strip has + on pin 2. IR was first wired reversed (from an earlier version of
the wiring page); swapped at J2. One IR pair saw up to 14 V in reverse (~7 V per LED, rated 5 V); all light now.

### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

### Superseded Battery energy section

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: battery current with white and IR at 25/50/100 % duty; mA per chain at full on (the clip lighting cost per second).

### Superseded status line

Status: **pass** (2026-10-16/18).

## Obstacles

- **White never lit; J1 + at 37 V** -> open chain; LEDs came on while an ohmmeter bridged J1 pin 2 to GND -> J1 pin 2
  (white -) not joined to its pad / the 2 mm bottom track to R66 -> resoldered.
- **The "wrong" chain lit** -> PWM0..2 share one counter; a channel's output is high while the counter is at or above
  its compare register, 0 after reset -> shell: disable the other channel first; firmware `led_set()` disables the
  off channels before enabling.
