# K230 console over the BL616 link

Since 2026-10-23 the K230's whole console, from the first U-Boot SPL line to msh and every program's output, reaches
the PC through the **BL616's USB port (PR1)**, not the CH340X on USB-C 2. One USB device carries both consoles:

| Port | Linux | Stable name | What |
|---|---|---|---|
| CDC port 0 (interfaces 0/1) | `/dev/ttyACM0` | `/dev/serial/by-id/usb-Bouffalo_Lab_Bouffalo_Lab_USB_Console_00000001-if00` | BL616 shell, printf |
| CDC port 1 (interfaces 2/3) | `/dev/ttyACM1` | `...-if02` | K230 console (read and type) |

Both are opened for everyone by `/etc/udev/rules.d/99-leakcam.rules` (copy: `tools/power-bringup/99-leakcam.rules`)
and need one VMware "connect to Linux" for the BL616. The K230 port stays when the K230 is power-cycled.

**Why** (user, 2026-10-22): the CH340X lives on the K230's switched 3V3, so it re-enumerated with every K230 power
cycle and needed a VMware click each time, and VMware stopped offering it (tests/test038); a K230 cold boot without
console was the norm. Bonus: the console now runs at 4 Mbaud instead of 115200, and the boot no longer waits for its
own log: **reset release to app init 0.484 s -> 0.30 s** (tests/test039, test022).

Only the K230 BootROM's own lines (errors, USB boot) stay on UART0 = CH340X: it is mask ROM.

## 1. How it works

```
 K230                                                              BL616                          PC
 SPL ─┐                                                            UART0 (IO21/22)
 OpenSBI ─┤ (before link_mux: straight to uart1)                    │
 RT-Smart kprintf/msh/stdout ─► "kcon" ─┐                           ▼
                                        ├─► UART1 IO40/41 ═══4 Mbaud═══► k230_linesplit ─► valid frame ─► link code
 leakcam_agent ─► /dev/link ────────────┘                                    │              (product: k230_link.c;
                                                                             │               test fw: "link rx:")
 msh (stdin) ◄─ "kcon" ◄─┐                                                   └─► text ─► USB CDC port 1 ─► /dev/ttyACM1
 leakcam_agent ◄─ /dev/link ◄─ valid frame ◄─ k230_linesplit ◄─ UART1 ◄════ UART0 ◄─ link frames / what is typed on ACM1
```

- **One wire, one rate.** The link frames (`$<seq>,CMD,...*XX\n`, CRC-8, firmware/bl616/k230_link.h) and the console
  text share UART1 <-> UART0 at `LINK_BAUD` = 4 Mbaud (tests/test018: clean over 10 MB, 6 Mbaud fails).
- **The same splitter on both ends** (`firmware/bl616/k230_linesplit.h`, header-only, no OS): a `$...*XX\n` line with
  a correct CRC is a frame, everything else is console text. Text is passed on at once (keystrokes do not wait for a
  newline); only a `$`-candidate is held until it completes or is proven not to be a frame. The CRC keeps console
  output containing `$` from being taken as a frame.
- **A console line and a frame written at the same moment may mix on the wire**: the frame then fails its CRC and the
  link resends it after `LINK_ACK_TIMEOUT_MS` (300 ms). Nothing is lost; the link protocol already covered losses.

## 2. Files in this repository

| File | Role |
|---|---|
| `firmware/bl616/k230_linesplit.h` | the splitter, shared by both BL616 firmwares and copied into the K230 kernel driver |
| `attic/firmware/bl616/test/test_linesplit.c` | host test: frames, text, `$` in text, bad CRC, a restarting candidate, over-long lines, in every chunking |
| `firmware/bl616/usb_console2.c`, `.h` | the two-port USB console (a copy of the SDK's `bsp/common/usb_app/usb_console.c`, SDK 2.3.35, with a second CDC ACM port: EP 0x81/0x02/0x86; strings 4/5 name the ports); `usb_k230_write()` to the host, `usb_k230_rx()` (weak) from it |
| `attic/firmware/bl616/CMakeLists.txt`, `tests/firmware/bl616_powertest/CMakeLists.txt` | swap the SDK's `usb_console.c` for `usb_console2.c` (see 3.1) |
| `attic/firmware/bl616/k230_link.c` (product) | `link_poll()` feeds the splitter: frames to `parse()`/`accept()`, text to `usb_k230_write()` |
| `tests/firmware/bl616_powertest/main.c` (test firmware) | `uart_bridge_task`: UART0 polled (as test018's echo, no byte lost at 4 Mbaud) into the splitter, text to port 1, frames printed as `link rx:`; `usb_k230_rx()` sends what is typed on ACM1 to UART0; the bridge starts by itself whenever the rails come up (`rails_on_timed()`); DTR (`console_open`) counts only port 0 |
| `firmware/k230_board/rtsmart/drivers/link_mux/link_mux.c`, `Kconfig`, `SConscript` | the K230 kernel driver (see 4) |
| `attic/firmware/k230_agent/leakcam_agent.c` | opens `/dev/link` instead of `/dev/uart1` (the `-u` option is gone) |
| `firmware/k230_board/rtsmart/configs/k230d_leakcam_defconfig` | `CONFIG_RT_CONSOLE_DEVICE_NAME="uart1"`, `CONFIG_RT_USING_LINK_MUX=y` |
| `firmware/k230_board/sdk/configs/k230d_rtos_leakcam_defconfig` | `CONFIG_RTT_CONSOLE_UART1=y`, `CONFIG_RTT_CONSOLE_ID=1`, `CONFIG_RTT_CONSOLE_BAUD=4000000` (also gives OpenSBI the UART1 address) |
| `firmware/k230_board/uboot/dts/k230d_leakcam.dts` | `aliases { uart1 = &serial1; }`, `chosen { stdout-path = "uart1:4000000n8"; }`, `&serial1 { clocks = <&def_100mhz>; }` |
| `firmware/k230_board/uboot/configs/k230d_leakcam_defconfig` | `CONFIG_BAUDRATE=4000000` (U-Boot ignores the rate in stdout-path) |
| `firmware/k230_board/install.sh` | installs the driver and applies the SDK patches (see 3, 4, 5) |
| `tests/test017-nand-raw-speed/leakcam_bench.c` (copy of the SDK's board bench) | `uart_bench` pauses the mux (`link_mux_pause()`) for its raw run |

## 3. BL616 side

### 3.1 The console swap (no SDK file edited)

The SDK builds its console from `CONFIG_BSP_CONSOLE_USB_CDC`: `bsp/common/CMakeLists.txt` adds
`usb_app/usb_console.c` to the `app` target, `board_init()` calls `usb_console_init()`, newlib's `write()` calls
`usb_console_write()`. The option must stay on (else printf goes to UART0, the K230 link). `find_package()` already
added the SDK file when our CMakeLists continues, so it is taken out and ours put in:

```cmake
get_target_property(_app_sources app SOURCES)
list(FILTER _app_sources EXCLUDE REGEX "usb_app/usb_console\\.c$")
set_property(TARGET app PROPERTY SOURCES ${_app_sources})
target_sources(app PRIVATE <path>/firmware/bl616/usb_console2.c)
target_include_directories(app PRIVATE ${BL_SDK_BASE}/bsp/common/usb_app)
```

`usb_console2.c` keeps the same function names, so `board_init()` and printf work unchanged. Check: the map file has
`usb_console2.c.obj` and no `usb_app/usb_console.c.obj`. On an SDK update, compare `usb_console2.c` with the new
`usb_console.c` (it says which version it copies).

## 4. K230 kernel: `link_mux` (`firmware/k230_board/rtsmart/drivers/link_mux/link_mux.c`)

Installed by `install.sh` into `$RTT/drivers/extdrv/link_mux/` together with `k230_linesplit.h` and `k230_link.h`
(from `firmware/bl616/`); one `source "drivers/extdrv/link_mux/Kconfig"` line is added to `extdrv/Kconfig` after
the bl616_nethub line (asserted). `extdrv/SConscript` builds every subfolder.

- At `INIT_COMPONENT_EXPORT` (after the UART driver, before finsh) it registers two character devices:
  - **`kcon`**: the console's I/O device. `rt_console_set_device("kcon")` makes RT-Smart's tty console (and so
    kprintf, msh and every program's stdin/stdout) use it. It answers `RT_DEVICE_CTRL_CONSOLE_OFLAG` (stream mode)
    and `RT_DEVICE_CTRL_NOTIFY_SET` (the tty console's receive callback); writes go to uart1 with `\n` -> `\r\n`.
  - **`link`** (`/dev/link`): the agent's frames, with DFS file operations like the serial driver's (non-blocking
    read returns 0, `poll()` reports POLLIN through the device wait queue). Writes go to uart1 unchanged.
- It then opens `uart1` itself (`RT_DEVICE_FLAG_INT_RX`) and sets its `rx_indicate`: in the receive interrupt the
  bytes go through the splitter, text into the `kcon` ring (2 KB) + the console's notify, frames into the `link` ring
  (1 KB) + a POLLIN wake-up. Whole frames only: one that does not fit is dropped (the sender resends).
- Until it starts, the console is `uart1` itself (`CONFIG_RT_CONSOLE_DEVICE_NAME="uart1"`, `CONFIG_RTT_CONSOLE_ID=1`
  at 4 Mbaud), so the earliest kernel lines go out the same wire.
- `link_mux_pause(1/0)`: `uart_bench` (test018) takes uart1 raw for its run.

The UART1 rate itself is set in the UART driver (`drv_uart.c`, install.sh "link baud" patch, 2026-10-22):

```c
        if (inst->uart.index == 1)
            inst->device.config.baud_rate = 4000000;   /* LEAKCAM: link baud (bl616/k230_link.h LINK_BAUD) */
```

## 5. SPL and OpenSBI at 4 Mbaud (SDK patches, `install.sh`)

The problem: SPL (U-Boot's `ns16550` driver) and OpenSBI clock the UART at 50 MHz with an integer divider, so their
fastest rate is 50 M / 16 = 3.125 Mbaud; OpenSBI at 4 Mbaud computed a divider of 0 (forced to 1, 3.125 Mbaud) and
the BL616 saw garbage. RT-Smart reaches 4 Mbaud because its `drv_uart.c` sets the UART clock divider to 1/1
(100 MHz) and uses the DesignWare fractional divisor: 100 M / (16 x 4 M) = 1.5625 = 1 + 9/16. The patches do the
same in SPL and OpenSBI (~20 lines; user: "patch", over the alternative of running everything at 3.125 Mbaud).

**K230 registers used:**

| Register | Address | Use |
|---|---|---|
| sysctl `uart_i2c_clkdiv_cfg` | `0x9110002C` | UART1 clock divider: bits 5:3 = denominator - 1 (default 1 = 1/2 = 50 MHz), bit 31 = write enable. Written as `(v & ~0x38) | (1 << 31)` = 1/1 = 100 MHz (as RT-Smart's `sysctl_clk_set_leaf_div(SYSCTL_CLK_UART_1_CLK, 1, 1)`) |
| UART1 DLF | `0x91401000 + 0xC0` | fractional divisor, 4 bits (sixteenths), written with DLAB set, as RT-Smart's `drv_uart_set_baudrate()` |

**Patch 1, U-Boot `src/uboot/uboot/drivers/serial/ns16550.c`, `ns16550_serial_probe()`** (marker `LEAKCAM: UART1
clock`), before `ns16550_init()`:

```c
	com_port->plat = dev_get_plat(dev);
	if (plat->base == 0x91401000) {	/* LEAKCAM: UART1 clock 1/1 = 100 MHz (console + BL616 link, 4 Mbaud) */
		u32 v = readl((void *)0x9110002c);
		writel((v & ~0x38u) | (1u << 31), (void *)0x9110002c);
	}
	ns16550_init(com_port, -1);
```

SPL has no board hook between DM start-up (`spl_early_init()`) and its console (`preloader_console_init()`), so the
driver's probe is the place; it touches only UART1.

**Patch 2, same file, `ns16550_serial_setbrg()`**: the divisor x 16, rounded; the integer part in DLL/DLH as before,
the sixteenths in DLF:

```c
	unsigned int x16 = (plat->clock + baudrate / 2) / baudrate;
	clock_divisor = x16 / 16 ? x16 / 16 : 1;
	ns16550_setbrg(com_port, clock_divisor);
	int lcr = serial_in(&com_port->lcr);
	serial_out(lcr | UART_LCR_DLAB, &com_port->lcr);
	writel(x16 / 16 ? x16 % 16 : 0, (void *)(plat->base + 0xc0));
	serial_out(lcr, &com_port->lcr);
```

(At 100 MHz and 4 Mbaud: x16 = 25 -> DLL 1, DLF 9.) The board dts gives serial1 the 100 MHz clock
(`clocks = <&def_100mhz>`) so `plat->clock` matches the hardware after patch 1.

**Patch 3, OpenSBI `src/opensbi/opensbi/platform/kendryte/fpgac908/platform.c`** (marker `LEAKCAM: UART clock`):

```c
#define UART_CLK                    100000000   /* LEAKCAM: UART clock 1/1, set by SPL (ns16550 patch) */
```

OpenSBI's 8250 driver (`lib/utils/serial/uart8250.c`) already computes DLL + DLF from `in_freq / baud`; its UART
address (`CONFIG_OPENSBI_CONSOLE_UART_REG_ADDR` = 0x91401000) and rate (`CONFIG_RTT_CONSOLE_BAUD`) come from the SDK
config (section 2). It relies on SPL having set the 1/1 clock.

Every patch is asserted (the build stops if the SDK code it expects has changed) and idempotent (a marker check).

## 6. Using it

- Read and type the K230 console on `/dev/ttyACM1` (any terminal; the CDC port ignores the rate setting).
- Scripts: `tests/test028-nand-power-loss/plt_loop.py` (`K230_CONSOLE`), skills' `console_record.py` and
  `boot_time.py` default to `/dev/ttyACM1`.
- The test firmware prints link frames on the BL616 shell (`link rx: $0,READY*23`); it has no link protocol, so the
  agent's "READY not answered" is expected there. The product answers them.
- One reader per port: stop an old `cat /dev/ttyACM1` before starting another (skill board-bringup).

## 7. Limits and open points

- The BootROM's own messages stay on UART0 (CH340X, 115200); they only appear on errors and in USB boot.
- A frame that collides with console output is resent (300 ms later), not lost.
- Console text and frames are capped at the rings: kcon 2 KB, link 1 KB (K230); USB port 1 ring 8 KB (BL616): a host
  that is not reading loses console text, as with any USB console.
- The product BL616 firmware build (usb_console2.c, the splitter in k230_link.c) is code-complete and host-tested,
  not yet built (the rewrite after the tests).

## 8. Verified (tests/test039, 2026-10-23)

- `/dev/ttyACM1` from power-on: `U-Boot SPL 2022.10 ...`, the DDR training lines, `OpenSBI v0.9`, RT-Smart, msh, the
  agent: **0 non-printable bytes**.
- `boot_times` / `chip_temp` typed on `/dev/ttyACM1` ran in msh; the agent's frames arrived separately as `link rx:`.
- 3 cold boots: kernel init start 0.100-0.104 s (was 0.197), app init 0.296-0.301 s (was 0.385; 0.484 with the
  115200 console).
- Host tests: test_linesplit, test_link (console text between frames), the agent's end-to-end link test.
