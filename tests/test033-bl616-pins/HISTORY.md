# test033: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-10-31 (run.py, 106 s): pass

pt_pins + pin_check.py:

- off: [pins_off.txt](pins_off.txt): 0 pin(s) off the table
- on_reset: [pins_on_reset.txt](pins_on_reset.txt): 0 pin(s) off the table
- running: [pins_running.txt](pins_running.txt): 0 pin(s) off the table
- boot1_high: [pins_boot1_high.txt](pins_boot1_high.txt): 0 pin(s) off the table
- uart_on: [pins_uart_on.txt](pins_uart_on.txt): 0 pin(s) off the table


### Automated run 2026-10-25 (run.py, 106 s): pass

pt_pins + pin_check.py:

- off: [pins_off.txt](pins_off.txt): 0 pin(s) off the table
- on_reset: [pins_on_reset.txt](pins_on_reset.txt): 0 pin(s) off the table
- running: [pins_running.txt](pins_running.txt): 0 pin(s) off the table
- boot1_high: [pins_boot1_high.txt](pins_boot1_high.txt): 0 pin(s) off the table
- uart_on: [pins_uart_on.txt](pins_uart_on.txt): 0 pin(s) off the table

### 2026-10-22

`pin_check.py` on every dump: **0 pins off the table in all 5 states** (35 pins each).

| Pin | Net | Rails off | On, reset held | Running | Notes |
|---|---|---|---|---|---|
| IO00 | K230_RSTN (1 = held) | out, pad 1 | out, pad 1 | out, pad 0 | reset held before the rails, released by pt_run |
| IO01 | K230_ALIVE | in, 0 | in, 0 | in, 1 | the K230's heartbeat pin high while it runs |
| IO02 | AI_BOOT strap | JTAG func (reset default), in, 0 | same | same | not driven; R35 holds it low: defined |
| IO03 | PGOOD | in, 0 (USB in) | same | same | product: analog (ACOMP0 wake) |
| IO10-12, 14, 15 | SDIO D2 D3 CMD D0 D1 | in, 0 | in, 1 | in, 1 | no drive, no pull: the 3V3 sense (0 / 5 of 5) |
| IO13 | SDIO CLK | analog | analog | analog | parked |
| IO16, 17 | Y3 | GPIO, ie 0 oe 0, no pull | same | same | the crystal runs (test032) |
| IO20 | LEAK_SENS | analog | analog | analog | ADC / ACOMP |
| IO21, 22 | UART0 link | analog | analog | analog; UART when on | on: UART0 TX (sig 9 = 2), RX (sig 10 = 3), SDK internal pull-up on |
| IO27 | BOOT1 | out, 0 | out, 0 | out, 0 (1 with pt_boot1 high) | NAND boot |
| IO28, 29 | AHT20 I2C | I2C0, drive 1, no pull | same | same | R73/R74 external |
| IO30 | K230_PWR | out, 0 | out, 1 | out, 1 | |
| IO4-9, 18, 19, 23-26, 31-34 | not connected | GPIO, ie 0 oe 0, no pull | same | same | reset default: input buffer off, high-Z: defined |

**Back-feed, rails off:** no K230-side pin (IO10-15, IO21, IO22) driven or pulled, IO01 an input; 3V3 sense 0 of
5 (below VIL); VBUS 63 mA (BL616 awake, K230 off); **K230 3V3 rail at C87.1 (test003 point 7): 0 V** (meter, 11:24): no back-feed.

**Found:**
- **The product firmware never drove IO27 (BOOT1).** Floating, BOOT1 read 0.5 V, at the 1.8 V bank's VIL
  (bugs/v2.txt item 7): the K230 could start SD/USB boot. The test firmware always drove it low, so every test
  booted NAND. Fixed: `PIN_K230_BOOT1` in board_pins.h (shared), driven low in `k230_power_init()` before the K230
  can leave reset; low is 0 V into an unpowered K230, so no back-feed.
- IO02 stays in the JTAG function (reset default, input) - harmless, R35 holds it low; IO21/22 get the SDK's
  internal pull-up while the UART is on (rails up, parked analog otherwise) - harmless.
- HBN state: the pins cannot report themselves in HBN; R19 (K230_PWR) and R76 (RSTN) hold the K230 off there, shown
  by test031 (1.23 mA asleep, the K230 stays off).

### Superseded status line (2026-10-22)

Status: **pass** (2026-10-22): every pin as in the table in 5 states (pt_pins + pin_check.py, 0 off); product fixed: BOOT1 (IO27) was never driven; no back-feed (rails off: no K230-side pin driven or pulled, 3V3 rail 0 V at C87).

## Obstacles

- **The first automated run (2026-10-25) reported IO00 missing in every state** -> the shell colours every line
  (ANSI codes before "pin 00") and pin_check.py's pattern missed it -> run.py strips the codes before saving.
- **IO21/IO22 as UART with the pull-up while the rails are on** failed the old table -> since test039 (2026-10-23)
  `pt_on` starts the K230 console bridge whenever the K230 has power: by design, no back-feed with 3V3 up -> the
  table (pin_check.py) now expects UART in every rails-on state.
- **Output levels read 0 everywhere at first** -> the SDK drives outputs in set/clear mode (bit 30): neither the
  config's bit 24 nor GLB_GPIO_CFG136 holds the level, and `bflb_gpio_read()` reads the input buffer, which is off on
  outputs (pt_on said "held" while IO00 read 0) -> pt_pins reads the pad with the input buffer on for that read.
- **UART signal read from the wrong register** (BL602 layout, both 0) -> BL616: signal = pin % 12 in GLB_UART_CFG1/2
  (0x154/0x158).
