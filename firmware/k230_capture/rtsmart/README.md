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

## leakcam_stream: live video (RTSP H.264, HTTP JPEG/MJPEG)

- `rtsp://<ip>:8554/cam0` and `/cam1`: H.264 main profile, 1280x960, 30 fps, CBR 1500 kbit/s per
  camera, IDR every 2 s and on every new client. Served by the SDK's `librtsp_server.a` (live555)
  through `rtsp_glue.cpp`.
- `http://<ip>:8080/snap/0.jpg`, `/snap/1.jpg` (one fresh JPEG) and `/mjpeg/0`, `/mjpeg/1`
  (multipart MJPEG, 5 fps). JPEGs are encoded only while a client wants one.
- Pipeline per camera: VICAP (offline mode, NV12) -> dump -> the same frame to the H.264 channel
  and, when wanted, the JPEG channel -> release. Four VENC channels, the hardware maximum.
- Budget: about 3 Mbit/s for both H.264 streams, inside the 10-25 Mbit/s expected from the BL616
  over SDIO. About 45 of the 70 MiB of MMZ.
- `leakcam_stream 1` streams camera 0 only.
- Needs a network interface: the BL616 Wi-Fi driver is still to come.
- Not verifiable from the source (binary MPP libraries), to check on the board:
  - that VENC keeps its own reference to a frame after `send_frame`;
  - that VICAP drops the 45 fps sensor rate to 30 fps in offline mode.

## leakcam_audio: microphone bench test

```
leakcam_audio [-d sec(10)] [-o file.wav] [-g 0|6|20|30] [-a alc_db] [-v adc_db] [-s skip_ms] [-r]
```

- **Recording.** 16 kHz, 16-bit mono from the codec's left input: U13 MSM381ACB026 on MICPL/MICNL
  through C97/C101. The SDK names that input `KD_I2S_IN_MONO_LEFT_CHANNEL`, "hp input", after the
  EVB, whose on-board mic is on the right input.
- **Output.** It writes `/sdcard/leakcam/audio-<time>.wav`, never overwriting an existing file,
  and prints RMS, peak, DC and clip count every second.
- **Gains.** Gains are set after `kd_mpi_ai_enable`, because the first enable resets them.
  Defaults: 30 dB mic PGA and +9 dB ALC. That overloads near 94 dB SPL, so if the clip count
  rises, use `-g 20`.
- **MIC_BIAS.** Neither the SDK nor the Linux driver ever writes the MIC_BIAS voltage field
  (codec register 0x80 bits 2:0), and no Canaan document gives the default. The mic needs 1.5 V
  or more.
  - **First bench check:** measure DC at C96 while recording.
  - `-r` dumps the register. Whether `kd_mpi_sys_mmap` maps the codec registers is not verified.

Hardware review of the mic path: the topology and values match the EVB headset mic (1 uF DC
blocks, pseudo-differential). Two rev 1 changes:
- a 1-4.7 uF capacitor directly on the MIC_BIAS ball (U3.A4); C96 is 20 mm away behind FB4;
- C97/C101 moved next to the K230; they are about 14 mm away today, at the mic end.

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
