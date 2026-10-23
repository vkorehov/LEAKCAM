# test039: K230 console over the BL616 link (no CH340)

Status: **pass, stage A** (2026-10-23): the kernel console, msh and the agent on `/dev/ttyACM1` (the BL616's second
USB port), typing reaches msh, link frames split out correctly; boot to app init 0.484 -> 0.385 s. Stage B open:
SPL and OpenSBI still print on UART0 (CH340).

Why: the CH340X (USB-C 2) runs from the K230's switched 3V3, re-enumerates with every K230 power cycle and needs a
VMware click each time, which VMware stopped offering (tests/test038). User (2026-10-22): forward the K230 output
over the BL616's UART, as early as possible, one USB device; fewest clicks.

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

Stage B (open): SPL and OpenSBI on UART1 at 4 Mbaud. SPL's UART clock is 50 MHz with an integer divider (U-Boot
ns16550): 4 Mbaud needs the 100 MHz clock (divider 1:1) and the DesignWare fractional divider (DLF), as RT-Smart's
drv_uart does - a U-Boot patch via install.sh. Then the K230 log is complete from the first SPL line; only the
BootROM stays on UART0 (mask ROM, prints only on errors / USB boot).

## Metrics

| Metric | Value | Note |
|---|---|---|
| Boot, power-on -> app init | 0.385 s | console at 4 Mbaud; was 0.484 s |
| Console path | UART1 4 Mbaud -> BL616 -> USB CDC | same wire as the link |
| Clicks per K230 power cycle | 0 (was 1 for the CH340) | the BL616 port stays |
| Image | kdimg 45.4 MB; BL616 test firmware ~153 KB | |
| Current | 568 mA (charging the cell, BL616 + K230 off, 08:33) | user reading |

## Obstacles

- **No BL616 on USB in the morning** (08:30): the Mac/VM had let go overnight; the user moved it to another port and
  connected it in VMware; the earlier `make flash` had left the chip in its ROM, the next flash used that.
- **K230 USB boot device not seen for 300 s** (first try): USB-C 1 was on the charger (568 mA charging), not the
  Mac -> moved to the Mac, flashed (25.8 s).
- **Design correction during the work:** "output only" was not possible - with the console on UART1, msh reads the
  same device and would swallow the agent's frames -> the split is symmetric (K230 link_mux + BL616 splitter).
- **A wrong test expectation** (my mistake): "$$" + a frame that itself starts with '$' is three '$', not two.
