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

`firmware/k230_board/install.sh <k230_rtos_sdk>` copies this folder to `src/applications/leakcam`
(sources in `src/`), registers it and enables `APP_ENABLE_LEAKCAM` through the LEAKCAM defconfig.
The board itself (pins, NAND, cameras, kernel config, image layout) is `firmware/k230_board/`; the
full procedure is `firmware/BUILD.md`. `leakcam_capture` and `leakcam_hist` land in `/sdcard/app/`.

## Not ported yet

- `k230_agent` (heartbeat GPIO, UART link to the BL616, ENV frame, orderly halt): RT-Smart has
  `/dev/gpio` (lseek + write of one byte, `KD_GPIO_IOCTL_SET_MODE`), `/dev/uart1`
  (`UART_IOCTL_SET_CONFIG`, `poll()` for timeouts) and no read-only remount; UFFS data is on the
  NAND after `fsync`.

## Verified so far

- Built inside the SDK with the LEAKCAM board (2026-09-24): both programs link against the real
  `libmpp` and are in the image's `/sdcard/app`. VICAP buffers come from pools VICAP creates
  itself (`buffer_pool_id = VB_INVALID_POOLID`, as the SDK samples): an id of 0 would put both
  cameras' raw and NV12 buffers into one 3-block pool.

- Every RT-Smart source compiles for `riscv64-linux-musl` with `-Wall -Wextra -Werror` against the
  CanMV MPP headers (`zig cc`, stand-in `k_autoconf_comm.h` with OV5647 and CSI 0/2 enabled).
  `vicap_cap.c` also compiles against the older k230_sdk MPP headers.
- Linking resolves everything except the 15 `kd_mpi_*` calls that `libmpp` provides;
  `leakcam_hist` links completely.
- The shared code passes the host tests with zlib and with miniz (and under ASan/UBSan).
- Nothing has run on a K230 yet.
