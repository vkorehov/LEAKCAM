# leakcam_capture on RT-Smart (k230_rtos_sdk)

The same program as the Linux build, with two platform files swapped:

| Part | Linux (k230_linux_sdk) | RT-Smart (k230_rtos_sdk / CanMV) |
|---|---|---|
| Cameras | `v4l2cap.c`, vvcam V4L2, `/dev/video0`, `/dev/video3` | `vicap_cap.c`, MPP VICAP devices 0 and 1, offline mode |
| LED PWM | `led_linux.c`, sysfs `pwmchip0` | `led_rtsmart.c`, `/dev/pwm` ioctl |
| Compression | system zlib | bundled miniz 3.0.2 (`third_party/miniz`, MIT) |
| State folder | `/var/lib/leakcam` | `/sdcard/leakcam` (UFFS, NAND partition `nand1`) |

Change detection, reference files and the image history are shared. The reference files use two
slots per image because UFFS cannot `rename()` onto an existing file; that code is the same on
both systems.

Target SDK: `kendryte/k230_rtos_sdk` with the CanMV rtsmart kernel and `canmv-k230/mpp` (manifest
`canmv-k230/manifest`). The older `kendryte/k230_sdk` is not usable here: its SPI-NAND driver only
knows the 1.8 V W25N01GW/W25N02JW IDs (EF BA xx), not the W25N02KV (EF AA 22) on this board.
`vicap_cap.c` also builds against the older MPP with `-DLEAKCAM_MPP_K230SDK`.

## Install into the SDK

```
ln -s <LEAKCAM>/firmware/k230_capture/rtsmart <k230_rtos_sdk>/src/applications/leakcam
echo 'subdirs-$(CONFIG_APP_ENABLE_LEAKCAM) += leakcam' >> <k230_rtos_sdk>/src/applications/apps.mk
cat <LEAKCAM>/firmware/k230_capture/rtsmart/Kconfig.app  # add to the applications Kconfig menu
make menuconfig   # enable APP_ENABLE_LEAKCAM
```

`leakcam_capture` and `leakcam_hist` land in `/sdcard/app/`.

## Board port still to do (the LEAKCAM board is not a stock SDK board)

1. **Sensors.** Enable `MPP_ENABLE_SENSOR_OV5647` with CSI devices 0 and 2
   (`MPP_ENABLE_CSI_DEV_0`, `MPP_ENABLE_CSI_DEV_2`): the 1280x960 mode names only exist with them.
   The OV5647 drivers take I2C bus, reset pin and MCLK from per-board settings; LEAKCAM needs
   CSI0 = CAM2 (J4) on `i2c0` (GPIO48/49) and CSI2 = CAM1 (J5) on `i2c4` (GPIO7/8). The CSI2 1280x960
   mode enables MCLK0 in the stock driver; the modules carry their own oscillator (to confirm),
   so no MCLK is needed.
2. **Camera pin 18** (GPIO62 / GPIO63). The drivers pulse a "CAM_PIN" as reset. If pin 18 is a
   plain GPIO on the modules, mux IO62/63 as GPIO and use them as CAM_PIN; if the modules take
   XCLK there, keep them as M_CLK and point CAM_PIN at an unused GPIO.
3. **Pin mux** (`board/configs/<board>/pinmux_config.c`): IO48/49 IIC0, IO7/8 IIC4,
   IO40/41 UART1 (sel 1, BL616 link), IO60/61 PWM0/PWM1 (sel 1), IO2 GPIO (heartbeat),
   IO14-19 for the SPI NAND.
4. **SPI NAND** (`LPKG_USING_SPINAND`, Winbond on): the W25N02KV is in `flash/winbond.c`
   (2048 + 128 OOB, 64 pages/block, 2048 blocks). The stock `k230_evb_spinand` layout is for a
   128 MB part with 64-byte OOB: rewrite `spinand_parts.h`, `genimage-spinand.cfg` and the kdimg
   OOB flag for 256 MB / 128 B OOB. Which QSPI controller the boot ROM uses on GPIO14-19 (OSPI
   sel 1 or QSPI0 sel 3) is not confirmed; the EVB uses OSPI (`spi0`).
5. **Clock.** No hardware RTC on the K230D here: enable `RT_USING_SOFT_RTC`, and set the time from
   the BL616 over the link at every wake (`clock_settime` needs an `rtc` device).
6. **Autostart.** `CONFIG_RTT_AUTO_EXEC_CMD` runs the agent after the NAND mounts.
7. **Memory.** Two cameras at 1280x960: about 26 MB of video buffers (3 raw + 3 NV12 per camera)
   plus ISP working memory; the K230D RT-only configs give 80 MB of MMZ.

## Not ported yet

- `k230_agent` (heartbeat GPIO, UART link to the BL616, ENV frame, orderly halt): RT-Smart has
  `/dev/gpio` (lseek + write of one byte, `KD_GPIO_IOCTL_SET_MODE`), `/dev/uart1`
  (`UART_IOCTL_SET_CONFIG`, `poll()` for timeouts) and no read-only remount; UFFS data is on the
  NAND after `fsync`.

## Verified so far

- Every RT-Smart source compiles for `riscv64-linux-musl` with `-Wall -Wextra -Werror` against the
  CanMV MPP headers (`zig cc`, stand-in `k_autoconf_comm.h` with OV5647 and CSI 0/2 enabled).
  `vicap_cap.c` also compiles against the older k230_sdk MPP headers.
- Linking resolves everything except the 15 `kd_mpi_*` calls that `libmpp` provides;
  `leakcam_hist` links completely.
- The shared code passes the host tests with zlib and with miniz (and under ASan/UBSan).
- Nothing has run on a K230 yet.
