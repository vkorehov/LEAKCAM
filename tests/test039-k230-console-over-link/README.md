# test039: K230 console over the BL616 link (no CH340)

Status: **fail** (2026-10-25): ~0.7 % of commands typed on `/dev/ttyACM1` are lost (993 of 1000 answered); otherwise the
whole K230 console, from U-Boot SPL on, runs at 4 Mbaud over the BL616 link with 0 garbage bytes (2026-10-23).

Why: the CH340X (USB-C 2) runs from the K230's switched 3V3, re-enumerates with every K230 power cycle and needs a
VMware click each time, which VMware stopped offering (tests/test038). User (2026-10-22): forward the K230 output
over the BL616's UART, as early as possible, one USB device; fewest clicks.

Design, files and every SDK patch with its location: [firmware/CONSOLE.md](../../firmware/CONSOLE.md).

## Setup

- **K230:** kernel driver `link_mux` (firmware/k230_board/rtsmart/drivers/link_mux): owns UART1 (4 Mbaud), splits
  what arrives with the shared k230_linesplit.h (valid frames `$...*XX\n` -> `/dev/link` for the agent, the rest ->
  console device `kcon`); `kcon` is the console's I/O device (`rt_console_set_device`), so kprintf, msh and every
  program's stdio use it. Console before the driver starts: `uart1` itself (`CONFIG_RTT_CONSOLE_ID 1`, 4000000).
  The agent opens `/dev/link`. `uart_bench` pauses the mux for its raw run.
- **BL616 test firmware:** two-port USB console `usb_console2.c` (one device ffff:ffff, interfaces 0/1 = BL616 shell
  `/dev/ttyACM0` `-if00`, 2/3 = K230 console `/dev/ttyACM1` `-if02`), swapped for the SDK's file in CMake; UART0
  bridge task (busy poll as test018's echo) feeding k230_linesplit: text -> port 1, frames -> `link rx:` on the
  shell; port 1 input -> UART0. Starts by itself whenever the rails come up.
- **Product BL616:** k230_link.c's link_poll uses the same splitter (frames -> parse/accept, text -> port 1).
- Host tests: test_linesplit (every chunking), test_link (console text between frames), the agent's e2e test.

## Needs

**MISSING**: the kernel options / drivers, SDK components, libraries and programs this test uses.

## Steps

**MISSING**: the steps of the test.

## Pass

**MISSING**: the pass criterion.

## Result

- USB: one device, two ports: `/dev/ttyACM0` (if00, BL616) and `/dev/ttyACM1` (if02, K230), both opened by the udev
  rule; 0 clicks per K230 power cycle, the BL616 port stays (2026-10-23).
- From power-on `/dev/ttyACM1` carries `U-Boot SPL 2022.10...`, the DDR training lines, `OpenSBI v0.9` and RT-Smart
  at 4 Mbaud with **0 non-printable bytes** (2026-10-23, stage B). Needs the install.sh patch: U-Boot ns16550 sets
  UART1's clock divider 1/2 -> 1/1 (100 MHz, sysctl 0x9110002c bits 5:3, bit 31 write enable) and writes the
  DesignWare fractional divisor (DLF at 0xC0: 4 Mbaud = 1 + 9/16); OpenSBI UART_CLK 100 MHz; dts `uart1` alias,
  `stdout-path = "uart1:4000000n8"`, serial1 on def_100mhz; U-Boot `CONFIG_BAUDRATE=4000000` (SPL and OpenSBI at
  50 MHz with an integer divider reach 3.125 Mbaud at most).
- Boot, 3 cold boots: kernel init start **0.100-0.104 s**, app init **0.296-0.301 s** (2026-10-23).
- Link frames split out: the agent's `$...*XX` frames came out on the BL616 shell as `link rx:`, none on the console;
  `boot_times`, `chip_temp` typed on `/dev/ttyACM1` ran in msh (2026-10-23).
- Command -> answer, host-timed: 55 / 55 / 111 ms (min / median / max), 0 late (> 2 s) (2026-10-25, run.py).
- BL616 bridge: UART RX interrupt into a 4 KB ring, the task blocks on it (priority 5); `rx dropped` 0 since
  (2026-10-24, test100). The product BL616 firmware has no console bridge; its link poll sleeps 5 ms per round.

Open:
- **~0.7 % of commands typed on /dev/ttyACM1 are lost** (993 of 1000 in 4 boots, 2026-10-25; 399 of 400 in 8 boots
  the same day): no echo of the command at all, so the line never reached the shell; a 5 s window changed nothing.
  Several coincide with the agent starting leakcam_wake (a 5 MB program load), the others with the agent's own
  console lines. Suspects: the K230's link_mux puts console input into kcon_rb without checking the space
  (link_mux.c, uart_rx_ind path), or the BL616 bridge loses USB OUT data while it forwards console output
  (usb_k230_rx -> UART0). Next: drop counters on both ends. The agent's link frames arrive the same way (they retry,
  LINK_TRIES 5, but a lost WAKE costs a resend).

## Metrics

| Metric | Value | Note |
|---|---|---|
| Boot, power-on -> app init | 0.296-0.301 s (3 cold boots) | console at 4 Mbaud from SPL |
| Console path | UART1 4 Mbaud -> BL616 -> USB CDC | same wire as the link |
| Clicks per K230 power cycle | 0 | the BL616 port stays |
| Image | kdimg 45.4 MB; BL616 test firmware ~153 KB | |
| Current | 568 mA (charging the cell, BL616 + K230 off, 08:33) | user reading |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 206440 / min 204396 / 206440 B, blocks 16 -> 16, deleted 0; K230 heap available 7025152 B steady; BL616 no leak (mem_leak.py, 8 snapshots); K230 no leak (mem_leak.py, 8 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| This part's boot share | link_mux registers at 0.27 s with the other drivers; the console is up from the SPL banner (2026-10-25) | - | driver / mount / probe |
| Test run time | 172 s automated (run.py, 2026-10-25), no setup | - | setup separately |
| Voltage under load (min) | VBAT 4.33 V min (K230 ADC ch 1, test023 calibration, 8 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | probe points named |
| Power, energy per operation | n/a: the console bridge's own current is below the meter's step next to the K230 (2026-10-25) | - |  |
| Per day | n/a: the console is a bring-up path, not used in a product session (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 39.5 / 42.2 / 40.7 C; board 32.6 / 32.7 / 32.4 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -O2 (SDK default), no LTO (2026-10-25) | smallest code (rule) |  |
| Features over the minimal image | link_mux in the kernel: 2.4 KB code + 3.5 KB RAM, part of the product; the BL616 bridge is test firmware (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (the UART line and USB CDC): compiler flags do not change the result (2026-10-25) | the faster one stays |  |

## Boot time

**MISSING**: the boot time of this test's image and its share.

## Battery energy

**MISSING**: current -> power -> energy per operation and per day.
