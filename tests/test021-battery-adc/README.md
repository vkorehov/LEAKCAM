# test021: Battery ADC (K230 ADC_1, VBAT/3)

Status: **planned** (2026-10-21).

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
