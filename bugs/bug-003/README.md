# bug-003: any access to the K230 PMU block hangs the K230

Found 2026-10-17 in the first NAND boot (tools/power-bringup README, K230 test record). Board: MIFA rev 1 (fab 11a6a38).

## Symptom

The boot runs ROM -> SPL (DDR trains at 2667, NAND found, TOC read) -> `OpenSBI v0.9` (built quiet: its only line)
-> RT-Smart, then goes silent: no kernel banner, no K230_ALIVE heartbeat, nothing at any baud rate.

Diagnostic prints (temporary, SDK only) narrowed it down:
- the kernel runs: `primary_cpu_entry`, BSS, `entry()`, `rt_hw_board_init`: MMU on, heap, IRQ, tick: all printed;
- `board_pinmux_init()` sets the bank voltage of pins 0..63, then hangs on **pin 64**, the first PMU pad
  (IO64..IO71 are in the PMU register block, `0x91000000 + 0x80`);
- with pins 64..71 skipped, the kernel continues: `heap: [...]`, `page: [...]`, the RT-Smart logo, then hangs
  again where the drivers start (the hardware RTC, `RT_USING_RTC_PMU`, is in the same PMU block).

Canaan's K230D boards (BPI CanMV-K230D-Zero, the base of this port) use the same 72-pad table and the PMU RTC,
so the PMU is accessible on a correctly working K230D.

## Cause (to find)

Two differences from the BPI reference:
1. **32.768 kHz crystal Y1** (Epson FC-135, `Q13FC13500004`, top side; C33/C34 20 pF to GND; XTAL_32K_IN/OUT on
   K230 balls D15/D16). The PMU logic runs from it; with no 32 kHz clock its registers may never answer.
2. **PMU supply.** AVDD1P8_RTC and AVDD1P8_LDO (balls D12/D11) are on the switched **1V8** rail, so the PMU
   powers up together with the core. On the BPI board they are on VDD_1V8_RTC, an always-on LDO (XC6206) that
   is up before everything else.

**Next:** scope on Y1 with the K230 rails on (`pt_on`): ~32.768 kHz expected about 1 s after 1V8 comes up
(the oscillator is analog, in the RTC domain; it needs no software). Probe XTAL_32K_OUT (Y1 pin 2 / C34) with a
10x probe; a probe on XTAL_32K_IN can stop a 32 kHz crystal. No oscillation: check the crystal's solder joints
and the load capacitors (a 2-pad 32 kHz crystal has no polarity). Oscillating: the PMU wants its supply before the core (point 2).

## What the documents say (checked 2026-10-17)

- **PMU User Guide:** "be sure to wake up the PMU module through any INT, otherwise the kernel will be stuck when
  initializing the driver" - this symptom exactly.
- **Hardware Design Guide (dev):** on a cold start the PMU only enables "the long press interrupt of INT0 and the
  high-level input interrupt of INT4"; "INT4 needs to be pulled up to the PMU power supply"; "AVDD1P8_RTC must not
  be powered up later than AVDD1P8_LDO" (met: same net); 32.768 kHz, 20 ppm, 1.8 V.
- **Crystal:** `Q13FC13500004` = FC-135, CL 12.5 pF, ESR <= 70 kOhm, +-20 ppm. C33/C34 2 x 20 pF = 10 pF + ~2-4 pF
  stray: matches. Same part and caps as the BL616's Y3, whose RTC runs.
- No public report of the same hang found; every Canaan/community board found powers the PMU always-on.

| | LEAKCAM rev 1 | BPI K230D-Zero |
|---|---|---|
| INT4 (F14) | R45 100 k to switched 1V8 | pull-up to VDD_1V8_RTC (always on) |
| AVDD1P8_RTC/LDO (D12/D11) | switched 1V8 | VDD_1V8_RTC (always-on LDO) |
| OUT0 / OUT1 | not connected | OUT0 drives a regulator enable |

**Leading theory:** INT4, the PMU supply and the core all rise together from the switched 1V8, so the PMU never
sees INT4 as a wake event (no edge, or sampled before its 32 kHz clock runs), stays asleep, and the first register
access blocks. Scope check: Y1 (XTAL_32K_OUT) oscillating and INT4 at 1.8 V with the rails on = crystal cleared,
cause = power-up order.

## Solution (rev 2)

To be decided by the scope check. Most likely: AVDD1P8_RTC/LDO and the INT4 pull-up on an always-on 1.8 V
supply (a small LDO from VBAT, as the BPI board does from VBUS), up before the core rails.

## Workaround (rev 1 boards)

**Test first (no parts):** `pt_on` (K230 held in reset), wait 5 s, short INT4 (R45's pad on the K230 side) to GND
for ~1 s, let go, then `pt_run`. Booting past the PMU = the power-up-order theory holds. Where:
[int4_edge_board.png](int4_edge_board.png), zoom [int4_edge.png](int4_edge.png). R45's other pad is 1V8: never short it.

**Fix: delay INT4 with a capacitor.** 10 uF from INT4 (R45's K230-side pad) to GND: with R45 (100 k) INT4 crosses
the 1.8 V input threshold (~0.65 x 1.8 V) after ~1.05 x RC = ~1 s, when the PMU is powered and its 32 kHz clock
runs, so it sees a clean rising level and wakes. The K230 first touches the PMU after SPL + DDR training,
~1.5-2 s after reset, so the edge comes in time. 4.7 uF ~0.5 s, 22 uF ~2.3 s (too close to the first access).

**Fallback:** the K230 must not touch the PMU: `BOARD_NOT_SUPPORT_HW_RTC`
 (64-pad pin table, no PMU RTC; as Canaan's
ATK-DNK230D board does). The time then comes from the BL616 alone, which already sends it in its WAKE frame.
Not applied yet: decided after the scope check.
