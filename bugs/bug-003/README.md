# bug-003: K230 hangs on the first PMU access (INT4 pull-up too weak)

Found 2026-10-17, solved 2026-10-20. Board: MIFA rev 1 (fab 11a6a38). Tests: [test007](../../tests/test007-rtsmart-boot/README.md).

## Symptom

The boot runs ROM -> SPL (DDR 2667, NAND) -> `OpenSBI v0.9`, then RT-Smart stops without a word: at the pin mux of
IO64 (the first PMU pad) or at the PMU RTC driver. No K230_ALIVE heartbeat.

## Cause

The PMU wakes at power-up only on a **high level on INT4** (ball F14); until then every access to its registers blocks.
On rev 1 INT4 has **R45 = 100 k** to 1V8, and the K230's INT4 pad pulls down with about 20 kOhm: INT4 measured
**0.3 V** with the rails on (R45's other pad 1.8 V). The PMU never woke.

The references pull INT4 up hard: Canaan's K230 EVB ties it straight to RTC_1V8 (0 Ohm, "power on by INT4 high
level"), the BPI CanMV-K230D-Zero uses R7 = 10 k.

## Fix

- **Rev 1:** bridge R45 with solder (INT4 = 1V8) and cover it with epoxy: [int4_bridge.png](int4_bridge.png), where on
  the board [int4_bridge_board.png](int4_bridge_board.png). Check: R45 pad 1 reads 1.8 V with the rails on.
  Our rev 1 boards always carry this fix; the firmware assumes it.
- **Rev 2:** INT4 tied to the PMU's 1.8 V, or a pull-up of 4.7 k or less (bugs/v2.txt item 6).
- **Firmware:** the PMU RTC is used (`RT_USING_RTC_PMU`, 72-pad pin table). A board without the bridge hangs after
  `OpenSBI v0.9`.

## Verified (2026-10-20)

R45 bridged, INT4 1.8 V, rails off 10 s, cold start: `rtc driver register OK`, the pin mux past IO64-IO71, `msh />`, the
agent's session runs to the end. Repeated after re-seating the bridge: same.
