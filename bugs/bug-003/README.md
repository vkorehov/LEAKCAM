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

## Solution (rev 2)

To be decided by the cause: crystal/load-capacitor fix, or AVDD1P8_RTC/LDO on an always-on 1.8 V supply
(e.g. a small LDO from VBAT, as the BPI board does from VBUS).

## Workaround (rev 1 boards)

The K230 must not touch the PMU: `BOARD_NOT_SUPPORT_HW_RTC` (64-pad pin table, no PMU RTC; as Canaan's
ATK-DNK230D board does). The time then comes from the BL616 alone, which already sends it in its WAKE frame.
Not applied yet: decided after the scope check.
