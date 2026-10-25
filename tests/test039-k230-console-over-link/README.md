# test039: K230 console over the BL616 link (no CH340)

Status: **pass, stage A** (2026-10-23); **open (2026-10-25): ~0.7 % of typed commands lost** (Results): the kernel console, msh and the agent on `/dev/ttyACM1` (the BL616's second
USB port), typing reaches msh, link frames split out correctly; boot to app init 0.484 -> 0.385 s. Stage B open:
SPL and OpenSBI still print on UART0 (CH340).

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

## Results

### 2026-10-23, stage A

| Check | Result |
|---|---|
| USB | one device, two ports: `/dev/ttyACM0` (if00, BL616) and `/dev/ttyACM1` (if02, K230), both opened by the udev rule, one VMware click |
| K230 cold boot (NAND) | the whole RT-Smart log on `/dev/ttyACM1` from the kernel on: `link_mux: console and /dev/link share uart1`, NAND, UFFS, SDK banner, `msh />`, the agent's lines |
| link frames | the agent's `$0,READY*23` ... `$15,SLEEP,3600*48` came out on the BL616 shell as `link rx:`, none on the console (the test firmware has no link protocol: "READY not answered" expected) |
| typing | `boot_times`, `chip_temp` typed on `/dev/ttyACM1` ran in msh, output back on it |
| boot to app init | **0.385 s** (was 0.484 s, test022): the console now runs at 4 Mbaud instead of 115200, the boot no longer waits for its own log |
| before the kernel | ~150 bytes of garbage on `/dev/ttyACM1` (the stages before RT-Smart's UART driver: other rate or pin toggling) |

### 2026-10-23, stage B: SPL and OpenSBI on UART1 at 4 Mbaud

SPL and OpenSBI clock the UART at 50 MHz with an integer divider: 3.125 Mbaud at most, and OpenSBI sent garbage at
4 Mbaud (divider 0 -> 1). Patched (install.sh, ~20 lines; user: "patch"): U-Boot ns16550 probe sets UART1's clock
divider 1/2 -> 1/1 (100 MHz, sysctl 0x9110002c bits 5:3, bit 31 write enable), setbrg writes the DesignWare
fractional divisor (DLF at 0xC0: 4 Mbaud = 1 + 9/16, as RT-Smart's drv_uart); OpenSBI's UART_CLK 50 -> 100 MHz (its
8250 driver already writes DLF); board dts `uart1` alias, `stdout-path = "uart1:4000000n8"`, serial1 on def_100mhz;
U-Boot `CONFIG_BAUDRATE=4000000` (U-Boot ignores the rate in stdout-path).

| Check | Result |
|---|---|
| `/dev/ttyACM1` from power-on | `U-Boot SPL 2022.10...`, the DDR training lines, `OpenSBI v0.9`, RT-Smart: **0 non-printable bytes** |
| boot, 3 cold boots | kernel init start **0.100-0.104 s** (was 0.197), app init **0.296-0.301 s** (was 0.385; 0.484 before the link console) |

### Automated run 2026-10-25 (run.py, 172 s): **FAIL**

8 boots, 399 of 400 `version` round trips answered; host-timed command -> answer 55 / 55 / 106 ms (min / median / max).

### Automated run 2026-10-25 (run.py, 165 s): **FAIL**

4 boots, 993 of 1000 `version` round trips answered; host-timed command -> answer 55 / 55 / 111 ms (min / median / max); 0 late (> 2 s).

**Finding: ~0.7 % of commands typed on /dev/ttyACM1 are lost** (none late: a 5 s window changed nothing; 2 of 400
and 1 of 400 in the shorter runs before). Each loss shows no echo of the command at all, so the line never reached
the shell. Several coincide with the agent starting leakcam_wake (a 5 MB program load, "capture failed" without
cameras), the others with the agent's own console lines. Two places can drop it: the K230's link_mux puts console
input into kcon_rb without checking the space (link_mux.c, uart_rx_ind path), or the BL616 bridge loses USB OUT data
while it forwards console output (usb_k230_rx -> UART0). Next: drop counters on both ends. It matters beyond the
console: the agent's link frames arrive the same way (they retry, LINK_TRIES 5, but a lost WAKE costs a resend).

## Metrics

| Metric | Value | Note |
|---|---|---|
| Boot, power-on -> app init | 0.296-0.301 s (3 cold boots) | console at 4 Mbaud from SPL; was 0.484 s |
| Console path | UART1 4 Mbaud -> BL616 -> USB CDC | same wire as the link |
| Clicks per K230 power cycle | 0 (was 1 for the CH340) | the BL616 port stays |
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

## Obstacles

- **No BL616 on USB in the morning** (08:30): the Mac/VM had let go overnight; the user moved it to another port and
  connected it in VMware; the earlier `make flash` had left the chip in its ROM, the next flash used that.
- **K230 USB boot device not seen for 300 s** (first try): USB-C 1 was on the charger (568 mA charging), not the
  Mac -> moved to the Mac, flashed (25.8 s).
- **Design correction during the work:** "output only" was not possible - with the console on UART1, msh reads the
  same device and would swallow the agent's frames -> the split is symmetric (K230 link_mux + BL616 splitter).
- **A wrong test expectation** (my mistake): "$$" + a frame that itself starts with '$' is three '$', not two.

- **The console bridge starved the BL616's IDLE task and later lost bytes** (2026-10-24, found in tests/test100) ->
  `k230con` polled the UART and only yielded while the K230 was on: at priority 1 IDLE never ran, deleted tasks were
  never freed and a Wi-Fi connect hung after a few joins; at idle priority it missed the 32-byte RX FIFO (80 us at
  4 Mbaud) under Wi-Fi load ("ip 10.63.22", cut tables). Fix: UART RX interrupt into a 4 KB ring, the task blocks on
  it (priority 5); `pt_status` shows `rx dropped` (0 since). The product firmware has no console bridge; its link
  poll sleeps 5 ms per round.

- lost `version` answer, raw: boot 5: 'leakcam_agent: READY not answered\nleakcam_agent: READY not answered\n' (2026-10-25, automated run)

- lost `version` answer, raw: boot 1 command 178 (29 s into the run): 'leakcam_agent: READY not answered\nleakcam_agent: READY not answered\nleakcam_agent: READY not answered\nleakcam_agent: READY not answered\n' (2026-10-25, automated run)
- lost `version` answer, raw: boot 1 command 238 (37 s into the run): 'cap: dev 0 init failed (sensor not answering on its I2C bus?)\ncapture failed\nleakcam_agent: hook exited 0, next wake in 3600 s\nleakcam_agent: SLEEP not answered\nleakcam_agent: done, heartbeat stopped\n' (2026-10-25, automated run)
- lost `version` answer, raw: boot 2 command 133 (68 s into the run): 'leakcam_agent: READY not answered\nleakcam_agent: READY not answered\nleakcam_agent: READY not answered\n' (2026-10-25, automated run)
- lost `version` answer, raw: boot 3 command 112 (104 s into the run): 'leakcam_agent: READY not answered\nleakcam_agent: READY not answered\nleakcam_agent: READY not answered\n' (2026-10-25, automated run)
- lost `version` answer, raw: boot 3 command 209 (114 s into the run): 'ason: cold\n[W/LWP] not use fast load, out of memory 8388608 < 15543000\nvicap: dev 0 init failed (sensor not answering on its I2C bus?)\ncapture failed\nleakcam_agent: hook exited 0, next wake in 3600 s\n' (2026-10-25, automated run)
- lost `version` answer, raw: boot 4 command 204 (151 s into the run): 'leakcam_agent: READY not answered\nleakcam_agent: READY not answered\nleakcam_agent: READY not answered\nleakcam_agent: READY not answered\n' (2026-10-25, automated run)
- lost `version` answer, raw: boot 4 command 248 (158 s into the run): 'cap: dev 0 init failed (sensor not answering on its I2C bus?)\ncapture failed\nleakcam_agent: hook exited 0, next wake in 3600 s\nleakcam_agent: SLEEP not answered\nleakcam_agent: done, heartbeat stopped\n' (2026-10-25, automated run)
