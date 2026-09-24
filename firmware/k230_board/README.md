# LEAKCAM board port for k230_rtos_sdk (RT-Smart only, SPI NAND boot)

Full procedure from an empty machine, including flashing both chips: `../BUILD.md`.

LEAKCAM as a board of Canaan's `kendryte/k230_rtos_sdk` (CanMV RT-Smart tree, manifest
`canmv-k230/manifest`): K230D SiP (128 MiB LPDDR4), W25N02KV 3.3 V SPI NAND on OSPI, two OV5647
(Pi v1.3 style, own oscillator), BL616 link on UART1, LED drivers on PWM0/PWM1.

```
repo init -u https://github.com/canmv-k230/manifest -b master --repo-url=https://github.com/canmv-k230/git-repo.git \
     -g sdk_super,sdk,sdk-common,sdk-special --depth=1 --no-clone-bundle && repo sync -c --no-tags -j4
cp firmware/k230_board/docker/leakcam_docker_build.sh <k230_rtos_sdk>/
mkdir -p <k230_rtos_sdk>/leakcam_docker && cp firmware/k230_board/docker/Dockerfile <k230_rtos_sdk>/leakcam_docker/
firmware/k230_board/install.sh <k230_rtos_sdk>
cd <k230_rtos_sdk>
./leakcam_docker_build.sh dl_toolchain                 # once
./leakcam_docker_build.sh k230d_rtos_leakcam_defconfig
./leakcam_docker_build.sh log                          # ~4 min on the 5-core arm64 box
# -> output/k230d_rtos_leakcam_defconfig/LEAKCAM_K230D_rtsmart_local_nncase_v2.11.0.kdimg (78 MB)
python3 <LEAKCAM>/firmware/k230_board/check_pad_voltage.py \
     output/k230d_rtos_leakcam_defconfig/uboot/spl/u-boot-spl.bin output/k230d_rtos_leakcam_defconfig/uboot/u-boot.bin
```

The Docker image is native arm64 Debian with amd64 runtime libraries: the SDK's RISC-V
toolchains and prebuilt host tools are x86-64 only and run through the host's qemu binfmt
handler; a full linux/amd64 container fails under qemu-user (bison/m4 crash).

## Verified by building (2026-09-24, k230_rtos_sdk 4162ec7, rtsmart 6cb12a8, u-boot c096dae)

- The whole image builds: SPL and U-Boot from source with the LEAKCAM device tree, OpenSBI from
  source, the RT-Smart kernel, the MPP camera stack and both LEAKCAM programs linked against the
  real `libmpp`, UFFS images, `.kdimg` with the partitions spl_a, spl_b, uboot, uboot_env,
  rtt_a, rtt_b, bin, sdcard and the TOC (chip K230D).
- The TOC decoded from the `.kdimg`: 9 entries at 0xe0000; rtt_a (5M) and rtt_b (25M) with
  load = 1, boot = 0x3; ota_meta at 1M. The SPL contains the TOC and A/B slot code
  (`k230_read_toc`, slot retry, ota_meta CRC check); it takes the load address from the uImage
  header in the slot, so the TOC's `load_addr` = 0 is right.
- The kernel has the SDIO stack, lwIP 2.1.2, the WLAN framework, ADC and the temperature sensor.
- The BL616 Wi-Fi driver (`bl616_nethub`) compiles without warnings and is linked into the kernel
  (`__rt_init_bl616_sdio_init` in `rtthread.elf`), with `RT_USING_BL616_NETHUB`, `BSP_USING_WIFI_SDIO`
  on SDIO0 and the SDIO0 reset output off in `rtconfig.h`. Not run: there is no board yet.
- `check_pad_voltage.py` on the built SPL and U-Boot: 64 pads each, all at the bank voltage of the
  board. The same check on Canaan's prebuilt EVB SPI-NAND SPL: 50 of 64 pads wrong.
- The SPL's device tree is LEAKCAM's (model "LEAKCAM K230D", SPI NAND quad on spi0); the kernel
  has the W25N02KV entry and the spinand/UFFS drivers; `/bin` carries the OV5647 ISP tuning files
  (1280x960 included), `/sdcard/app` the two programs.
- Not verified without hardware: that the boot ROM reads the NAND on a 3.3 V bank, DDR training,
  the sensors answering on i2c0/i2c4, and UFFS mounting on the real chip.

## Changes install.sh makes to SDK files

- `boards/Kconfig`: the LEAKCAM board entry; `arch/riscv/dts/Makefile` (U-Boot): the dtb.
- `drivers/extdrv/spinand/spinand_port.c` (RT-Smart): UFFS partitions get `oob_size = min(chip, 64)`.
  The W25N02KV has 128 spare bytes, but 64-127 are its on-die ECC parity (U-Boot's layout: free
  bytes 2-15, 18-31, 34-47, 50-63), and UFFS allocates 64-byte spare buffers for 2 KiB pages while
  using `ecc_size = oob_size - 32`. With 64 the geometry is the tested K230 EVB one and matches
  `mkuffs`, which rejects spare sizes above 64.
- Deletes `output/k230d_rtos_leakcam_defconfig/rtsmart/kernel/board/pinmux.o`: `pinmux.c`
  includes the board file through a macro that scons does not track.
- `drivers/extdrv/Kconfig` (RT-Smart): one `source` line for `drivers/extdrv/bl616_nethub/Kconfig`.
  `extdrv/SConscript` already builds every subfolder that has a SConscript.
- `rt-thread/components/drivers/sdio/sdio.c`: for function 1 of 424c:0606 only, a CIS without a
  FUNCE tuple gets `max_blk_size = 512` and a 200 ms enable timeout. The core otherwise refuses a
  function with a zero block size, and the BL616 SDU publishes none (Bouffalo's Linux host driver
  sets both values by hand for the same reason).

## Files

| Here | Installed as (in the SDK) | What |
|---|---|---|
| `pins.py` | (generator) | the one pin table; writes the two files below |
| `rtsmart/board/k230d_leakcam/pinmux_config.c` | `src/rtsmart/rtsmart/kernel/bsp/maix3/board/configs/k230d_leakcam/` | RT-Smart pin mux (generated) |
| `rtsmart/board/k230d_leakcam/spinand_parts.h` | same folder | UFFS partitions `nand0` (/bin) and `nand1` (/sdcard) |
| `rtsmart/configs/k230d_leakcam_defconfig` | `.../bsp/maix3/configs/` | RT-Smart kernel config |
| `uboot/dts/k230d_leakcam.dts`, `k230d_leakcam_pins.dtsi` | `src/uboot/uboot/arch/riscv/dts/` | U-Boot/SPL device tree (pins generated) |
| `uboot/configs/k230d_leakcam_defconfig` | `src/uboot/uboot/configs/` | U-Boot/SPL config |
| `sdk/configs/k230d_rtos_leakcam_defconfig` | `configs/` | top-level board config, cameras |
| `sdk/boards/k230d_leakcam/` | `boards/k230d_leakcam/` | U-Boot env, NAND image layout |
| `../k230_capture/rtsmart/` | `src/applications/leakcam/` (sources in `src/`) | the capture app |
| `rtsmart/drivers/bl616_nethub/` + `../bl616_wifi/wifi_ctrl_proto.h` | `.../bsp/maix3/drivers/extdrv/bl616_nethub/` | BL616 Wi-Fi driver, see `../bl616_wifi/README.md` |

`install.sh` also adds the board to `boards/Kconfig` and the dtb to U-Boot's dts Makefile.

## Decisions and why

- **SPL/U-Boot are built from source, never Canaan's prebuilt EVB SPI-NAND binaries.** Those set
  50 of 64 pads to 1.8 V mode (`check_pad_voltage.py` shows which); every LEAKCAM bank is 3.3 V and
  a wrong bank voltage can damage the chip. The open-source SPL boots from SPI NAND
  (`board/kendryte/common/k230_spl.c`: U-Boot at 2 MiB, RT-Smart at 5 MiB) and its Winbond
  driver knows the W25N02KV.
- **U-Boot reuses the BPI-Zero target** (`TARGET_K230D_CANMV_BPI_ZERO`): same K230D SiP, generic
  board code and DDR init; only the device tree and defconfig are LEAKCAM's.
- **NAND on OSPI (`spi0`, mux 1 on IO14-IO19), quad, 50 MHz**, as Canaan's EVB SPI-NAND setup and
  the SPI-NAND burn tool. The K230 guide lists QSPI0 on the same pads; the ROM sets its own mux.
- **Boot ROM and 3.3 V:** the pads reset in 3.3 V mode (MSC = 0), which is what the ROM reads the
  NAND with; the SPI-NAND burn tool relies on the same reset state. No Canaan board boots NAND
  from a 3.3 V bank, so the first power-up is the proof; if it failed, BOOT1 = 1 (B1 short or
  BL616 IO27) still forces USB flashing.
- **Cameras:** CSI0 = CAM2 (J4) on `i2c0`, CSI2 = CAM1 (J5) on `i2c4`, `MCLK_INVALID` (the modules'
  own 25 MHz oscillator, as the Raspberry Pi OV5647 overlay's fixed 25 MHz clock), no reset or
  power-down GPIO: connector pin 18 (IO62/IO63) is the module's LED line and is held low.
- **Direct boot, one image layout.** ROM -> SPL -> OpenSBI + RT-Smart: the SPL reads the TOC at
  0xe0000 and starts slot A (slot B if A does not load, or whichever `ota_meta` names). U-Boot
  stays in the image only as the SPL's fallback when there is no valid TOC. There is no other
  layout or boot path to choose from.
- **Kernel trimmed to what LEAKCAM uses.** Removed: touch, WS2812, one-wire, Realtek Wi-Fi, USB
  device, FAT, crypto, FFT, hardware timers, soft I2C, I2C1-3, SPI1-2, regulator, fast-boot
  preload, CanMV network manager. Kept because the SDK needs them or LEAKCAM will: SDIO0 (BL616
  Wi-Fi), lwIP + SAL + WLAN framework, NTP, ADC and TS (the SDK's pm and audio code link against
  them), UFFS on SPI NAND, PWM, UART, I2C0/I2C4.
- **OpenSBI from source**.
- **Clock: the K230's hardware RTC** (PMU, `RT_USING_RTC_PMU`, device `rtc`) on its own 32.768 kHz
  crystal. Its supply AVDD1P8_RTC is the switched 1V8 rail, so it starts from a default date at
  every power-up; the agent sets it from the BL616's `TIME` frame at every boot (the BL616's HBN
  RTC keeps the time across K230 power-offs). The PMU pads IO64-IO71 stay GPIO; the pin mux has 72
  entries.
- **NAND layout (256 MiB, 2048 + 128 B pages):** SPL 0 / 512K, TOC 896K, ota_meta 1M, U-Boot 2M,
  env 4M, RT-Smart slot A 5M and slot B 25M (20M each), `/bin` UFFS 48-68M, `/sdcard` UFFS 72-248M
  (image history, 2 x 32 MiB quota), last 8 MiB free. `genimage-spinand.cfg`, `spinand_parts.h`
  and the U-Boot dts partitions must agree.

## Not in this port yet

- **Wi-Fi on hardware.** The BL616 driver (`bl616_nethub`, NetHub over SDIO, `wlan0` with DHCP on
  the K230) builds and links, but has never talked to a BL616; `../bl616_wifi/README.md` lists what to
  check first.
- **OTA writer.** The SPL reads `ota_meta` and both slots; nothing on RT-Smart writes the
  inactive slot and the slot record yet.
- **The power agent on RT-Smart** (heartbeat, UART link, orderly halt): not ported;
  `RTT_AUTO_EXEC_CMD` stays empty until it is.
