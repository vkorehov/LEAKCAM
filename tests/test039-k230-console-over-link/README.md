# test039: K230 console over the BL616 link (no CH340)

Status: **pass** (2026-10-30): 1000 of 1000 commands answered in each of 4 boots at 2 Mbaud, every drop counter 0;
the whole K230 console, from U-Boot SPL on, runs over the BL616 link with 0 garbage bytes.

Why: the CH340X (USB-C 2) runs from the K230's switched 3V3, re-enumerates with every K230 power cycle and needs a
VMware click each time, which VMware stopped offering (tests/test038). User (2026-10-22): forward the K230 output
over the BL616's UART, as early as possible, one USB device; fewest clicks.

Design, files and every SDK patch with its location: [firmware/CONSOLE.md](../../firmware/CONSOLE.md).

## Setup

- **K230:** kernel driver `link_mux` (firmware/k230_board/rtsmart/drivers/link_mux): owns UART1 (2 Mbaud), splits
  what arrives with the shared k230_linesplit.h (valid frames `$...*XX\n` -> `/dev/link` for the agent, the rest ->
  console device `kcon`); `kcon` is the console's I/O device (`rt_console_set_device`), so kprintf, msh and every
  program's stdio use it. Console before the driver starts: `uart1` itself (`CONFIG_RTT_CONSOLE_ID 1`, 2000000).
  The agent opens `/dev/link`. `uart_bench` pauses the mux for its raw run.
- **BL616 test firmware:** two-port USB console `usb_console2.c` (one device ffff:ffff, interfaces 0/1 = BL616 shell
  `/dev/ttyACM0` `-if00`, 2/3 = K230 console `/dev/ttyACM1` `-if02`), swapped for the SDK's file in CMake; UART0
  bridge task (busy poll as test018's echo) feeding k230_linesplit: text -> port 1, frames -> `link rx:` on the
  shell; port 1 input -> UART0. Starts by itself whenever the rails come up.
- **Product BL616:** k230_link.c's link_poll uses the same splitter (frames -> parse/accept, text -> port 1).
- Host tests: test_linesplit (every chunking), test_link (console text between frames), the agent's e2e test.

## Needs

K230: link_mux driver in the kernel (UART1 shared by the console and the link, 2 Mbaud), msh. BL616: test firmware with
the 2-port USB CDC (`/dev/ttyACM0` BL616, `/dev/ttyACM1` K230 console) and the UART0 bridge task with k230_linesplit.
Host: udev rule 99-leakcam.rules, tools/test-standard/harness.py.

## Steps

`run.py`: 4 cold boots (`pt_off`, `pt_run`) to msh on `/dev/ttyACM1`; per boot 250 `version` commands, 5 s per answer,
host-timed; `pt_status` (BL616 console counters) and `link_stat` (K230 rx, drops, overruns) before and after; memory
sampled per boot.

## Pass

Every `version` answered (0 lost), none later than 2 s; byte counters equal on every hop (host -> BL616 -> K230), 0
overruns and 0 dropped bytes.

## Result

- USB: one device, two ports: `/dev/ttyACM0` (if00, BL616) and `/dev/ttyACM1` (if02, K230), both opened by the udev
  rule; 0 clicks per K230 power cycle, the BL616 port stays (2026-10-23).
- From power-on `/dev/ttyACM1` carries `U-Boot SPL 2022.10...`, the DDR training lines, `OpenSBI v0.9` and RT-Smart
  with **0 non-printable bytes** (2026-10-23, stage B; at 2 Mbaud since 2026-10-30). Needs the install.sh patch: U-Boot ns16550 sets
  UART1's clock divider 1/2 -> 1/1 (100 MHz, sysctl 0x9110002c bits 5:3, bit 31 write enable) and writes the
  DesignWare fractional divisor (DLF at 0xC0: 2 Mbaud = 3 + 2/16); OpenSBI UART_CLK 100 MHz; dts `uart1` alias,
  `stdout-path = "uart1:2000000n8"`, serial1 on def_100mhz; U-Boot `CONFIG_BAUDRATE=2000000`, the SDK's
  `CONFIG_RTT_CONSOLE_BAUD=2000000` (OpenSBI); install.sh takes the kernel driver's rate from bl616/k230_link.h.
- Boot, 3 cold boots: kernel init start **0.100-0.104 s**, app init **0.296-0.301 s** (2026-10-23).
- Link frames split out: the agent's `$...*XX` frames came out on the BL616 shell as `link rx:`, none on the console;
  `boot_times`, `chip_temp` typed on `/dev/ttyACM1` ran in msh (2026-10-23).
- Command -> answer, host-timed: 55 / 55 / 70 ms (min / median / max), 0 late (> 2 s) (2026-10-30, run.py).
- Lost commands at 4 Mbaud (~0.5 %): drop counters on both ends (BL616 `pt_status` "K230 console", K230 `link_stat`)
  showed every byte leaving the BL616 and fewer arriving in the K230's driver (k230_rx 2244-2252 of 2261), no
  overrun, no ring drop: lost on the wire. Bursts with idle gaps (`pt_uart burst`) lost 0-42 B of 2700 at 4 Mbaud
  and 0 at 2 Mbaud (10 bursts each); 3 and 2.5 Mbaud gave no working link. 2 Mbaud adopted chain-wide
  (2026-10-30): 4 boots x 1000 commands, all counters equal, 0 lost.
- BL616 bridge: UART RX interrupt into a 4 KB ring, the task blocks on it (priority 5); `rx dropped` 0 since
  (2026-10-24, test100). The product BL616 firmware has no console bridge; its link poll sleeps 5 ms per round.

Open:
- None.

## Metrics

| Metric | Value | Note |
|---|---|---|
| Boot, power-on -> app init | 0.296-0.301 s (3 cold boots) | console from SPL |
| Console path | UART1 2 Mbaud -> BL616 -> USB CDC | same wire as the link |
| Clicks per K230 power cycle | 0 | the BL616 port stays |
| Image | kdimg 45.4 MB; BL616 test firmware ~153 KB | |
| Current | idle 0.220 A (test006); `version` back to back over the link 0.226 A at 5 V (USB-C 1 VBUS meter, cell full, 2026-10-31) | user reading |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 206440 / min 204396 / 206440 B, blocks 16 -> 16, deleted 0; K230 heap available 7025152 B steady; BL616 no leak (mem_leak.py, 8 snapshots); K230 no leak (mem_leak.py, 8 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| This part's boot share | link_mux registers at 0.27 s with the other drivers; the console is up from the SPL banner (2026-10-25) | - | driver / mount / probe |
| Test run time | 172 s automated (run.py, 2026-10-25), no setup | - | setup separately |
| Voltage under load (min) | VBAT 4.33 V min (K230 ADC ch 1, test023 calibration, 8 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | probe points named |
| Power, energy per operation | 226 mA x 4.34 V = 981 mW; per command (55 ms) 0.054 J; over idle (+6 mA) 1.4 mJ (2026-10-31) | - |  |
| Per day | n/a: the console is a bring-up path, not used in a product session (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 39.5 / 42.2 / 40.7 C; board 32.6 / 32.7 / 32.4 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -O2 (SDK default), no LTO (2026-10-25) | smallest code (rule) |  |
| Features over the minimal image | link_mux in the kernel: 2.4 KB code + 3.5 KB RAM, part of the product; the BL616 bridge is test firmware (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (the UART line and USB CDC): compiler flags do not change the result (2026-10-25) | the faster one stays |  |

## Boot time

pt_run -> app init 0.296-0.301 s (3 cold boots); the console is up from the SPL banner, link_mux registers at 0.27 s with the other drivers (2026-10-25).

## Battery energy

226 mA while answering commands back to back, +6 mA over msh idle: 1.4 mJ per command above idle (2026-10-31). Per day:
n/a, the console is a bring-up path, not used in a product session.
