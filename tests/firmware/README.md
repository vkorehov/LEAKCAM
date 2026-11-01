# Test firmware

Everything here runs on the board for the tests; none of it is the product (firmware/). Host tools are in tools/.

- [bl616_powertest/](bl616_powertest): the BL616 test firmware (all `pt_*` commands, the proven modules from
  firmware/bl616 linked in).
- [k230_apps/](k230_apps): the K230 test image programs (leakcam_agent, leakcam_wake, leakcam_stream, leakcam_hist,
  the change kmodel); a copy of the first product code, the attic is never read.
- [k230_hello/](k230_hello): bare-metal K230 check from SRAM (no NAND needed).
- [test_install.sh](test_install.sh): builds all of it: firmware/k230_board/install.sh, the K230 test image, the
  BL616 test firmware (`tests/firmware/test_install.sh [SDK_DIR]`).

## BL616 test firmware

[bl616_powertest/](bl616_powertest): switches the K230 rails and tests the BL616-side hardware
from the PR1 console. Not the product firmware; flash that afterwards.

```
cd bl616_powertest
make                              # arm64 host: builds in the K230 build container by itself
make flash                        # running firmware -> ROM, flashes, boots it, waits for the console
```

A `make` with nothing changed grows the .bin by one DWARF backtrace table each time (2026-11-01: 1317904 ->
1338384 -> 1358864 B): the SDK's post_build step (cmake/bflb_flash.cmake, CONFIG_BACKTRACE) appends dwarfcfi.bin to
the .bin in place, and the .bin is only made fresh from the ELF on a relink. The code is the same; before a flash after
such builds, `touch main.c && make` (the size goes back).

On an arm64 host the RISC-V toolchain (x86-64 only) runs in the `k230-rtos-sdk-build:arm64-x86tc`
container from [firmware/BUILD.md](../../firmware/BUILD.md) section 2.2, which `make` starts; the
SDK sits next to LEAKCAM (`~/k230d-hw/bouffalo_sdk`). `make flash` runs in the same container with the
x86-64 `BLFlashCommand-ubuntu` (the SDK's `BLFlashCommand-arm` is 32-bit armhf and does not start on an arm64-only
host). Port permissions: install [99-leakcam.rules](../../tools/power-bringup/99-leakcam.rules) in /etc/udev/rules.d (all four board USB devices, every re-plug).

Rails start **off**; nothing switches until you type. Commands:

| Command | Does | Expect |
|---|---|---|
| `pt_status` | everything at a glance (printed when the console opens) | |
| `pt_mon [on\|off]` | one status line per second | |
| `pt_on` | K230 rails on, K230 held in reset, 3V3 ramp timed | `OK: 3V3 up ~3 ms`; then measure with `tools/power-bringup/power_check.py flashed` |
| `pt_off` | rails off, 3V3 discharge timed | `OK: 3V3 gone 56-82 ms` |
| `pt_run` / `pt_hold` | release / hold the K230 reset | blank NAND: the K230 ROM enumerates on USB-C 1 as `29f1:0230` (all four K230 rails good) |
| `pt_cycle [n]` | n power cycles with min/max timing | |
| `pt_uart on\|off\|send <text>` | UART0 = the K230 link (IO21 TX, IO22 RX, 115200 8N1): one message per second, received bytes echoed | only while 3V3 is up |
| `pt_i2c [scan]` | AHT20 humidity/temperature, or a scan of I2C0 | `0x38` |
| `pt_usb` | USB cable on USB-C 1 (BQ24072 PGOOD); plug/unplug is reported live | |
| `pt_leak` | leak node (IO20, ADC) and the resistance across S1..S3 | dry ~1650 mV; a 100k across S1 reads ~100 kohm |
| `pt_sdio watch [s]` / `drive <hex>` / `park` | SDIO lines to K230 MMC0: edge counts while the K230 probes; CMD/D0-D3 driven (bit 0 CMD .. bit 4 D3, rails on only) for the K230 to read | CLK and CMD toggle; every pattern read back (tests/test014) |

The BL616 sees the switched 3V3 through the SDIO pull-ups (read as inputs only, never driven);
0V8 is implied (3V3's regulator is enabled by 0V8's power-good); 1V8 and 1V1 need the multimeter
or `pt_run`. Each test can be left out: set it to 0 in
[tests.h](bl616_powertest/tests.h) and rebuild.

## K230 hello (no NAND needed)

[k230_hello/](k230_hello): 0.8 KB of bare-metal RISC-V that the boot ROM loads into SRAM over USB-C 1 and runs:
a status line per second on the K230 console (the BL616's `/dev/ttyACM1`; before 2026-10-22 USB-C 2 `/dev/ttyUSB0`) and a heartbeat on K230_ALIVE.
For checking a board's K230 before the NAND holds anything.

```
cd k230_hello
make                 # builds in the K230 build container
make run             # K230 in USB boot mode (29f1:0230; after pt_on + pt_run on a blank NAND)
```

In this state UART0 register reads return 0 (writes work), so the program paces characters by time rather
than polling THRE. The flasher (`k230_flash_cli`) is x86-64 only; `make run` runs it in the build container
with `timeout` inside the container: a `timeout` around `docker run` only kills the client and leaves the
container holding the K230's USB.
