# bug-001: U1 is BQ24072T, battery never charges

Found 2026-10-17 in the charging bring-up (tools/power-bringup, charging.png). Board: MIFA rev 1 (fab 11a6a38).

## Symptom

USB-C 1 in (VUSB 5.2 V), cell on JB1 at 3.9 V:
- VBAT (U1 OUT) 4.2 V, but BAT stays at 3.9 V: no charge current;
- red D1 (/CHG) on;
- the NTC on JT reads 10 k (room temperature, so it is fine).

## Cause

- The schematic symbol for U1 (LEAKCAM.SCHLIB CMP-0321-00268-2, POWER.SchDoc) is **BQ24072TRGTR**, LCSC C129319, datasheet field SLUS937A. The BOM and CPL carried it unchanged, and JLC fitted what was ordered.
- The TS circuit was drawn from the plain **BQ24072** datasheet (bq24072.pdf in the repo, SLUS810K): a 10 k NTC straight from TS to GND, biased by the chip's internal 75 uA source.
- The T variant has **no internal TS bias**. It compares TS with fractions of VIN: VHOT = 0.125 x VIN, VCOLD = 0.25 x VIN.
- With only the NTC there, TS sits at about 0 V. The chip reads that as "too hot" and suspends charging.
- /CHG stays low during a TS fault, so D1 stays on.

Nobody checked the part's suffix against the datasheet the circuit came from.

## Solution (rev 2)

Change U1 to **BQ24072RGTR**, LCSC **C140288**:
- JLC extended part, 3771 in stock on 2026-10-17, 1.05 USD (the T is 2.17);
- same QFN-16 3x3 package and pinout;
- the NTC works as drawn, so nothing else on the sheet changes.

Set the symbol's PartNumber, Description (C140288) and DatasheetVersion (SLUS810K), then regenerate the BOM. Also correct DESIGN.md and EXTERNAL_PARTS.md, which call the part BQ24072.

## Workarounds (rev 1 boards)

The thermistor is MF52D-103F-3435: 10 k, B3435 (EXTERNAL_PARTS.md). Charging runs while TS is between 0.125 and 0.25 x VIN, which is 0.65-1.3 V at 5.2 V. The cell temperatures below are calculated for B3435.

### A. One resistor, the bench workaround

![47 k from VUSB to TS](workaround_47k.png)

- Fit a leaded **47 k** from **C2 pad 1 (VUSB)** to **JT pin 1 (TS)**. The NTC stays on JT.
- C2 pad 2, right next to pad 1, is GND. Solder on the outer end of pad 1 only and sleeve the lead.
- Pads checked against the fab netlist: C2.1 is VUSB; JT.1 goes only to U1.1 (TS).
- TS at 25 C is about 0.91 V. It charges with the cell at about **14-36 C**; 43 k gives about 16-38 C.
- Protection stays on:
  - the cell outside that window pauses charging;
  - a broken NTC lead pulls TS to VUSB ("cold") and a shorted one pulls it to 0 V ("hot"): both stop charging;
  - U1 still has its 125 C junction regulation, thermal shutdown and the safety timer (R21 47 k).
- The window is narrow: charging may pause in a warm room or when the cell warms during a 0.89 A charge.

### B. Three resistors, TI's application circuit (SLUS937, 10.2.1.2.4)

- **8.25 k from TS to GND**, soldered across the JT pads.
- **19.1 k from VUSB to TS**.
- **100 k in series into the TS pin**: needed only if the TS track is cut; TS is a high-impedance input.

It charges from about **0 to 50 C**. With E24 parts, use 18 k + 1 k in series with 8.2 k (about 1-50 C). Don't use 20 k / 8.2 k: it charges below 0 C.

### C. Replace U1

Swap U1 for BQ24072RGTR (C140288) by hot air. The original circuit then works unchanged. It is the cleanest fix for boards that go into an enclosure.

### Without the thermistor

10 k across JT plus 47 k from VUSB to TS also charges, but the cell has **no temperature protection**. Use it only for a supervised bench test.

## Check after any workaround

With USB in:
- TS (JT 1 to JT 2) is about 0.9 V (A) or about 1.0 V (B) at room temperature;
- BAT rises from the cell voltage toward 4.20 V;
- D1 stays on until the cell is full.

Record the readings in the charging test record in tools/power-bringup/README.md.

## Lesson

For every chip, check the exact orderable suffix against the datasheet its circuit was drawn from. The symbol's datasheet field names that datasheet.
