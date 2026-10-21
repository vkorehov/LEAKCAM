# test022: Battery ADC (K230 ADC_1, VBAT/3)

Status: **in progress** (2026-10-21): USB case measured; battery case open.

K230 ADC_1 (ball A6) reads VBAT through R59 200 k / R61 100 k, switched by Q1 from the K230's 3V3 (readable only
while the K230 runs). The agent converts raw x 1800 mV x 3 / 4095 (12-bit, 1.8 V reference). The bug-004 rev 1
workaround cuts off on this reading (3.10 V under load), so it must agree with a meter.

## Setup

K230 running (BL616 test firmware: `pt_on`, `pt_run`), K230 console (USBC2), meter on C11 pad 1 (BAT) and C15 pad 1
(VBAT, what the ADC sees), GND on the caps' other pads.

![probe points](adc_points.png)

[adc_points.png](adc_points.png): VBAT on C15 pad 1, BAT on C11 pad 1, GND on each cap's other pad (top side, U1 corner).

## Steps

1. K230 shell: `adc probe adc`, then `adc read 1` five times; VBAT = raw x 5400 / 4095 mV.
2. USB in (charging): ADC vs meter VBAT and BAT.
3. USB out (battery): ADC vs meter; repeat at a lower cell voltage later (after a discharge), and with the KPU or
   LEDs loading the cell (the cutoff reads under load).

## Pass

ADC within +-30 mV of the meter's VBAT (after a one-point correction if it is a constant offset), noise <= 10 mV
between reads.


## Results (2026-10-21, USB powered, K230 at msh, NAND 80 MHz image)

`adc probe adc`, `adc read 1` x 20, VBAT = raw x 5400 / 4095:

| Point | Meter (second meter) | ADC |
|---|---|---|
| C15 pad 1, "VBAT" net = ADC input | 4.37 V | mean 4.430 V (+60 mV, +1.4 %), sd 52 mV, 4.340-4.507 |
| C11 pad 1, BAT | 3.298 V (board side of the open JB1 joint; the cell read 3.69 V at its wires) | - |
| Repeat 19:30, cell wired to C11, charging (CV): C15 | 4.37 V | 64 reads: mean 4.428 V (+58 mV), sd 49 mV, 4.345-4.509 |
| Repeat 19:30: C11, BAT | 4.18 V | - |

- **On USB, C15 is the BQ24072 output rail (~4.4 V), not the cell.** On battery the BAT-FET joins OUT to the cell, so
  the ADC reads the cell then: the only case the bug-004 cutoff needs. The firmware must not report the USB reading
  as battery level.
- **Noise sd 52 mV** (target 10): single reads are not enough for a 3.10 V cutoff -> average (64 reads: ~7 mV).
- Offset +60 mV: one-point correction once the battery case is measured.
- **C11 at 3.30 V with D1 off** (should charge, D1 on): the JB1 cell joint was open - see Obstacles.

## Metrics (2026-10-21)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init | 0.88 s | test021 | |
| This part's boot share | none (ADC probed on demand) | - | |
| Test run time | 20 reads ~45 s | - | |
| ADC accuracy / noise | +60 mV / sd 52 mV | +-30 mV / 10 mV | noise fails single-read |
| Current during reads | 202-203 mA (= msh idle) | floor 202 | reads cost nothing measurable |
| Power, energy per reading | below the meter's resolution | - | |
| Per day | n/a | - | |

## Boot time

n/a: one conversion during the session (not at boot).

## Battery energy

n/a for the reading itself (Q1 switches the divider only while the K230 runs: ~14 uA then, nothing while off); the test gives the voltage the energy audit (test104) logs.

## Obstacles

- **Unexplained board reset at 15:20:09 (2026-10-21)** while VBAT was probed at C15 with a second meter: BL616 and
  CH340 dropped together, the BL616 came back with its defaults. Not explained (probe slip or cable move); repeat
  the VBAT reading with the probe held firm, rails off first.
- **Cell at 3.30 V, D1 off, board resets when the ammeter leads are re-seated** (16:06, 16:07; and 14:42, 15:20) ->
  the charger does not see the cell (test001: D1 flashing then off = battery detection, no cell), so nothing backs
  up VBUS -> found: the cell's + wire joint on JB1 pin 1 was cracked (test001), reflowed.
- **"Empty cell" was not empty** -> first blamed on the burned ammeter fuse (~14:42-15:00, board on the cell with
  the K230 at ~200 mA, no cutoff: bug-004); the cell read 3.69 V at its wires, so it was the open JB1 joint, not a
  drain or a tripped PCM -> cell wired to C11, charged on the board (test001); then the battery case of this test.

