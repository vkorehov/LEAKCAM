# test022: Battery ADC (K230 ADC_1, VBAT/3)

Status: **pass** at 4.07-4.37 V (2026-10-21), calibration in the agent (committed 94d6b9e, not yet on the board's image). Open: a low-cell point (~3.4 V) and a loaded case.

K230 ADC_1 (ball A6) reads VBAT through R59 200 k / R61 100 k, switched by Q1 from the K230's 3V3 (readable only
while the K230 runs). The agent converts raw x 1800 mV x 3 / 4095 (12-bit, 1.8 V reference). The bug-004 rev 1
workaround cuts off on this reading (3.10 V under load), so it must agree with a meter.

## Setup

K230 running (BL616 test firmware: `pt_on`, `pt_run`), K230 console (USBC2), meter on C11 pad 1 (BAT) and C15 pad 1
(VBAT, what the ADC sees), GND on the caps' other pads.

![probe points](adc_points.png)

[adc_points.png](adc_points.png): VBAT on C15 pad 1, BAT on C11 pad 1, GND on each cap's other pad (top side, U1 corner).

## Needs (build features and dependencies)

K230 ADC driver (adc probe/read), msh adc command.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

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
| **Battery** 19:32 (USB out, K230 at msh): C15 | 4.12 V | 62 reads: mean 4.185 V (+65 mV, +1.6 %), sd 42 mV, 4.105-4.254 |
| Battery 19:32: C15 | 4.12 V | 64 reads: mean 4.178 V (+58 mV) |
| Battery 19:34: C15 | 4.11 V | 64 reads: mean 4.157 V (+47 mV) |
| Battery 19:36: C15 | 4.10 V | 64 reads: mean 4.152 V (+52 mV) |
| Battery 19:40: C15 | 4.07 V | 64 reads: mean 4.122 V (+52 mV) |

- **On USB, C15 is the BQ24072 output rail (~4.4 V), not the cell.** On battery the BAT-FET joins OUT to the cell, so
  the ADC reads the cell then: the only case the bug-004 cutoff needs. The firmware must not report the USB reading
  as battery level.
- **Noise sd 52 mV** (target 10): single reads are not enough for a 3.10 V cutoff -> average (64 reads: ~7 mV).
- Offset +47..+65 mV on USB and on battery (mean +55): constant within the meter's 10 mV step and the time between
  meter and ADC, so one correction, -55 mV (or x 0.986;
  the two points are too close in voltage to tell offset from gain; the low-cell point decides). Corrected and
  averaged over 64 reads: within +-10 mV of the meter: **pass** at a full cell.
- **Calibration (2026-10-21), 7 pairs 4.07-4.37 V:** gain fits as well as offset (residuals within +-10 mV both
  ways); gain chosen (a divider/reference error scales): meter = ADC x 0.9868, i.e. full scale 5329 mV instead of
  5400. In firmware/k230_agent/leakcam_agent.c (BAT_FULL_SCALE_MV), with the mean of 64 conversions (sd 45 -> ~6
  mV). The low-cell point will tell gain from offset; re-fit then.
- **C11 at 3.30 V with D1 off** (should charge, D1 on): the JB1 cell joint was open - see Obstacles.

## Metrics (2026-10-21)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1 (ADC input), BAT C11.1 (adc_points.png).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init | 0.88 s | test021 | |
| This part's boot share | none (ADC probed on demand) | - | |
| Test run time | 20 reads ~45 s; 64 reads ~16 s (script, 0.25 s each); battery case 19:31-19:41 | - | |
| ADC accuracy / noise | +60 mV / sd 52 mV single; calibrated 64-read mean within +-10 mV, sd ~6 mV | +-30 mV / 10 mV | pass averaged |
| Current on battery, K230 at msh | not measured (meter in USB-C 1 path, unplugged); 202-203 mA on USB (above) | - | VBAT 4.07-4.12 V |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Current during reads | 202-203 mA (= msh idle) | floor 202 | reads cost nothing measurable |
| Power, energy per reading | below the meter's resolution | - | |
| Per day | n/a | - | |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | kdimg 77.8 MB (/sdcard 176 MiB, /bin 20 MiB layout), kernel ~3.7 MB | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, --gc-sections (no -ffunction-sections, no LTO) | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test038's floor | test038 floor | bytes, boot ms, RAM, current |

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
- **Battery case: VBAT 4.11 V for a moment, then BAT and VBAT both ~1.5 V, board reset** (2026-10-21 ~19:35; K230
  running at msh, cell 4.18 V just before, cell's + wired to C11, USB-C 1 unplugged, the CH340 on USB-C 2 left in) -> the cell
  stopped feeding BAT under the ~200 mA load; the BL616 came back with defaults (K230 off) once USB was back. Cause
  open: the cell's - joint on JB1 pin 2 (same kind of joint as the failed +), the new C11 wire, or the pack's PCM
  latching -> measure the cell at its own wires, and its - wire to board GND (C11 pad 2). Not reproduced: at 19:31
  the same switch-over (K230 started on battery) held, VBAT 4.15 V -> 4.12 V under load, no reset; a repeat at
  19:32 read 4.178 V (+58 mV). Most likely cause (user): the battery shorted by accident while the probes were
  moved; the pack's PCM cut on the short and the board reset with no cell behind it. The PCM stayed latched (BAT
  ~1.5 V) with the board still on it as a load, and released only when USB was plugged back: after a short the
  pack needs the charger to recover (usual for DW01-type PCMs). Product: a short leaves the unit dead until USB.
- **CH340 re-enumerated (VMware popup) when USB-C 1 was plugged back at 19:41**, its own cable (USB-C 2) in all
  the time, K230 running, BL616 not reset -> not explained. U2 runs from the switched 3V3 (bug-002), so it stayed
  up on battery; a candidate is a short dip on VBAT/3V3 when U1 switches OUT from the cell to the input -> connect
  in VMware again.
  **Repeated 19:51** (dmesg + both consoles logged): unplug USB-C 1 -> nothing (BL616 "unplugged", CH340 stays);
  plug -> the CH340 drops at once (USB disconnect, then a new device 10 s later); BL616 only logs "plugged in", no
  reset; K230 at msh afterwards (a reboot of it not ruled out: its console was gone during the drop). Reproducible.
  Candidates: (a) D3's VCC is VUSB, back-fed to ~2.2 V from U2's D+ while unplugged (bug-002); the step to 5 V on
  plug couples into D+/D- and the host sees a disconnect; (b) a dip on the switched 3V3 that resets U2. Both go
  away in rev 2 (U2 on USBC2's VBUS, D3 VCC off VUSB: bugs/v2.txt 3/5). Scope 3V3 (U2 VCC) and D+ on the plug to tell.
- **BL616 console gone after the battery reading (2026-10-21 22:16)** -> a PR1 cable pin had slipped out of
  alignment (moved while probing / unplugging USB-C 1) -> re-seated; not a firmware or power event.
