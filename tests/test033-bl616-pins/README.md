# test033: every BL616 pin: function, levels, back-feed

Status: **pass** (2026-10-25): every pin as in the table in all 5 states (pt_pins + pin_check.py, 0 off); BOOT1 (IO27)
driven low by the product; no back-feed with the rails off (3V3 rail 0 V at C87, 2026-10-22).

The K230's pads were checked by configuration (check_pad_voltage.py: every bank's voltage against the board, before
each flash) and by line tests with both ends (test014 SDIO). This is the same for every BL616 (Ai-M62-CBS, U11) pin.

## Pins (board_pins.h, WIFI.SchDoc)

| Pin | Net | Function | Checks |
|---|---|---|---|
| IO00 | K230_RSTN (Q4 gate, R76 100 k up) | output, high = K230 held in reset | reset holds/releases (pt_hold/pt_run), level in HBN (R76) |
| IO01 | K230_ALIVE (R32 100 k to 3V3) | input | heartbeat edges (test024), parked while 3V3 off |
| IO02 | AI_BOOT (R35 100 k down, PR1.4) | BL616 boot strap | low at every reset; flashing link works |
| IO03 | PGOOD (R23 100 k to 3V3_SLEEP) | ACOMP0 / input | USB detect, HBN wake (test035) |
| IO10-IO15 | SDIO D2, D3, CMD, CLK, D0, D1 (10 k up to 3V3) | SDU / parked | test014 lines, test015 speed; parked (analog) while 3V3 off |
| IO16, IO17 | Y3 32.768 kHz crystal | XTAL32K | oscillates (test032) |
| IO20 | LEAK_SENS (1 M / 1 M, C109) | ACOMP1 / ADC0 | dry ~1.65 V, wet, HBN wake (test034) |
| IO21, IO22 | UART0 to K230 UART1 (R34/R36 up to 3V3) | UART / parked | test010, test018; parked while 3V3 off |
| IO27 | BOOT1 via RBT1 10 k | output | NAND boot low (reads 0.5 V parked: v2 item 7), USB boot high |
| IO28, IO29 | AHT20 I2C (R73/R74 4.7 k to 3V3_SLEEP) | I2C | reads (pt_i2c), idle high |
| IO30 | K230_PWR (R51, R19 100 k down) | output | rails on/off, low in HBN (R19) |
| EN | AI_EN (R38, PR1.5) | chip enable | reset from PR1 |
| D+, D- | PR1 USB | USB console | enumerates |
| NC pins | - | unused | defined state, no float (pull or analog) |

## Setup

BL616 test firmware with `pt_pins` (PT_TEST_PINS): every GPIO 0-34 from GLB_GPIO_CFGn (0x200008C4 + 4 n): function,
ie / oe / pu / pd / drive / Schmitt, the pad level, and for UART pins the signal matrix entry. Output levels are read
at the pad (input buffer on for one read; the SDK's set/clear output mode hides them elsewhere). Dumps in this folder
(`pins_<state>.txt`), checked by [tools/power-bringup/pin_check.py](../../tools/power-bringup/pin_check.py) against the
table above (board_pins.h + schematic notes). States: rails off; rails on, K230 in reset; K230 running from NAND;
BOOT1 high; UART link on. USB-C 1 VBUS meter.

## Needs (build features and dependencies)

BL616 GPIO (all pins), board_pins.h; product init code.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Configuration: each pin's function, pull, drive strength and IO voltage domain (3V3_SLEEP) in both firmwares
   (test and product) against this table: a script over board_pins.h and the init code, like check_pad_voltage.py.
2. Levels with a meter or the BL616's own read-back, in each power state: K230 rails off, rails on in reset, K230
   running, BL616 in HBN (test031).
3. Drive and read back where the other end is reachable (RSTN, K230_PWR via the rails, BOOT1 via the K230 ROM's
   boot path, UART and SDIO done in test010/014).
4. Back-feed: with the K230 rails off, no pin toward the K230 above ~0.1 V and the switched 3V3 at 0 V (firmware/sim
   predicts the cost of a mistake).

## Pass

Every pin as in the table in every state; no back-feed; unused pins in a defined state.

## Result

- `pin_check.py` on every dump: 0 pins off the table in all 5 states (35 pins each): rails off
  [pins_off.txt](pins_off.txt), rails on with reset held [pins_on_reset.txt](pins_on_reset.txt), K230 running
  [pins_running.txt](pins_running.txt), BOOT1 high [pins_boot1_high.txt](pins_boot1_high.txt), UART link on
  [pins_uart_on.txt](pins_uart_on.txt) (2026-10-25, run.py).
- **Back-feed, rails off:** no K230-side pin (IO10-15, IO21, IO22) driven or pulled, IO01 an input; 3V3 sense 0 of 5
  (below VIL); **K230 3V3 rail at C87.1: 0 V** (meter): no back-feed (2026-10-22).
- **BOOT1 (IO27):** the product firmware never drove it; floating it read 0.5 V, at the 1.8 V bank's VIL (bugs/v2.txt
  item 7), so the K230 could start SD/USB boot. Fixed: `PIN_K230_BOOT1` in board_pins.h (shared), driven low in
  `k230_power_init()` before the K230 can leave reset; low is 0 V into an unpowered K230, no back-feed (2026-10-22).
- IO21/IO22 are UART0 with the SDK's internal pull-up whenever the K230 has power (`pt_on` starts the K230 console
  bridge since test039, 2026-10-23), parked analog otherwise: by design, the table expects it (2026-10-25).
- IO02 stays in the JTAG function (reset default, input): harmless, R35 holds it low. Unconnected pins (IO4-9, 18,
  19, 23-26, 31-34): reset default, input buffer off, high-Z: defined (2026-10-22).
- HBN state: the pins cannot report themselves in HBN; R19 (K230_PWR) and R76 (RSTN) hold the K230 off there, shown
  by test031 (1.23 mA asleep, the K230 stays off) (2026-10-22).

Open:

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - each pin's level (table), 3V3 switched rail, VBAT C15.1.

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: a pin check; the boot is test022 (2026-10-25) | target (test022) | longest step: |
| This part's boot share | n/a: pin init is part of the BL616's main() (test021) (2026-10-25) | - | |
| Test run time | ~4 min of dumps (5 states); setup: 4 builds + flashes (~2 clicks each) | - | setup (flashing, plugging, waiting) separately |
| Speed / rate (none for this test) | n/a: a pin check (2026-10-25) | - | % of the top |
| Current idle / during (min / avg / peak) | 63 mA rails off (BL616 awake, USB VBUS); the dumps are instant | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | K230 3V3 rail 0 V with the rails off (C87.1, back-feed check) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a: pins cost nothing measurable (test031 pull audit) (2026-10-25) | - | scripts/energy.py |
| Per day | n/a: as energy (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 40.7 / 40.7 / 40.7 C; board 34.8 / 34.8 / 34.0 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 206440 / min 204396 / 206440 B, blocks 16 -> 16, deleted 0; K230 heap available 7025152 B steady; leak: n/a, 5 snapshots (mem_leak.py needs >= 8 cycles) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | BL616 test firmware 1 114 864 B at the 2026-10-25 run (with the Wi-Fi set of test100); ~151 KB before it (2026-10-22) | minimal image (rule) | the image this run used |
| Compile flags | BL616 test firmware -Os -g (the SDK's Kconfig default; bl616_powertest/defconfig sets none), sections, --gc-sections, no LTO | smallest code (rule) | |
| Features over the minimal image | BL616 test firmware commands (pt_*); test firmware 1 114 864 B; not in the product firmware (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

n/a for the pins themselves; the pin init time at boot is part of test021.

## Battery energy

A floating or back-feeding pin shows as extra sleep current: compare test031 before and after any pin change.
