# leakcam programs on RT-Smart (k230_rtos_sdk)

| Program | Sources | What it does |
|---|---|---|
| `leakcam_agent` | `../../k230_agent/leakcam_agent.c`, `../../bl616/k230_link.h` | BL616 link, heartbeat, hook, Wi-Fi request (started at boot) |
| `leakcam_wake` | `leakcam_wake.c`, `vicap_cap.c` (MPP VICAP devices 0 and 1, offline mode), `led_rtsmart.c` (`/dev/pwm`), `netclient.c`, core | the wake algorithm (DESIGN.md 5.1): capture, compare with the history, server check, video, history |
| `leakcam_hist` | `leakcam_hist.c`, core | list and rebuild stored frames |
| `leakcam_stream` | `leakcam_stream.c`, `flv.c`, `rtsp_glue.cpp`, `netclient.c` | video with audio: 5 s push to the server on a leak (`-p host:port -t 5`), or live for the bench, below |

Core: `imgdiff.c`, `refstore.c`, `history.c` and the bundled miniz 3.0.2 (`third_party/miniz`,
MIT) for the history compression. State lives in `/sdcard/leakcam` (UFFS, NAND partition
`nand1`). The reference files use two slots per image because UFFS cannot `rename()` onto an
existing file.

Target SDK: `kendryte/k230_rtos_sdk` with the CanMV rtsmart kernel and `canmv-k230/mpp` (manifest
`canmv-k230/manifest`). The older `kendryte/k230_sdk` is not usable here: its SPI-NAND driver only
knows the 1.8 V W25N01GW/W25N02JW IDs (EF BA xx), not the W25N02KV (EF AA 22) on this board.

## Install into the SDK

`firmware/k230_board/install.sh <k230_rtos_sdk>` copies this folder to `src/applications/leakcam`
(sources in `src/`), registers it and enables `APP_ENABLE_LEAKCAM` through the LEAKCAM defconfig.
The board itself (pins, NAND, cameras, kernel config, image layout) is `firmware/k230_board/`; the
full procedure is `firmware/BUILD.md`. All programs land in `/sdcard/app/`.

## leakcam_stream push mode: 5 s to the server on a leak

`leakcam_stream -p <host>:<port> -t <seconds>`, started by `leakcam_wake` when the server answers
`{"leak":true}`. One chunked `POST /v1/video?cam=<N>` per camera, both at once: an FLV (`flv.c`)
with the camera's H.264, starting on an IDR, and the microphone as G.711 mu-law 8 kHz mono, both
stamped from the moment the cameras start. No RTSP. Exit 0 when the server answered 200 for both
uploads. `ffplay` / VLC play the saved uploads as they are.

## leakcam_stream live mode: RTSP (bench)

- `rtsp://<ip>:8554/cam0` and `/cam1`: H.264 main profile, 1280x960, 30 fps, CBR 1500 kbit/s per
  camera, IDR every 2 s and on every new client. Served by the SDK's `librtsp_server.a` (live555)
  through `rtsp_glue.cpp`.
- Both sessions carry the microphone as G711U, the only audio type the SDK's server has. H.264
  only, no JPEG or MJPEG.
- Pipeline per camera: VICAP (offline mode, NV12) -> dump -> the H.264 channel -> release. Two
  VENC channels.
- Budget: about 3 Mbit/s for both H.264 streams plus 64 kbit/s of audio each, inside the 10-25 Mbit/s expected from the BL616
  over SDIO. Under 45 of the 70 MiB of MMZ.
- `leakcam_stream 1` streams camera 0 only.
- Needs the network: the hook (or whoever starts it) asks the agent for Wi-Fi first.
- Not verifiable from the source (binary MPP libraries), to check on the board:
  - that VENC keeps its own reference to a frame after `send_frame`;
  - that VICAP drops the 45 fps sensor rate to 30 fps in offline mode.

## Microphone (in every stream)

- **Capture.** 8 kHz, 16-bit mono from the codec's left input: U13 MSM381ACB026 on MICPL/MICNL
  through C97/C101. The SDK names that input `KD_I2S_IN_MONO_LEFT_CHANNEL`, "hp input", after the
  EVB, whose on-board mic is on the right input. Encoded as G.711 mu-law (flv.h): 64 kbit/s, a few
  operations a sample, no library.
- **Gain.** 30 dB mic PGA, set after `kd_mpi_ai_enable`, because the first enable resets it. The
  AI is enabled before the cameras, so its ~2 s codec power-up is over by the first frame.
- **MIC_BIAS.** Neither the SDK nor the Linux driver ever writes the MIC_BIAS voltage field
  (codec register 0x80 bits 2:0), and no Canaan document gives the default. The mic needs 1.5 V
  or more. **First bench check:** measure DC at C96 while a stream runs.

Hardware review of the mic path: the topology and values match the EVB headset mic (1 uF DC
blocks, pseudo-differential). Two rev 1 changes:
- a 1-4.7 uF capacitor directly on the MIC_BIAS ball (U3.A4); C96 is 20 mm away behind FB4;
- C97/C101 moved next to the K230; they are about 14 mm away today, at the mic end.

## Verified so far

- Built inside the SDK with the LEAKCAM board (2026-09-27): all five programs link against the
  real `libmpp` and are in the image's `/sdcard/app`. VICAP buffers come from pools VICAP creates
  itself (`buffer_pool_id = VB_INVALID_POOLID`, as the SDK samples): an id of 0 would put both
  cameras' raw and NV12 buffers into one 3-block pool.
- The wake algorithm runs on the host against `mock_server.py` (`make test`: cameras and LEDs
  from `test/wake_stub.c`, `test/fake_stream` in place of the push mode); the shared code passes
  its host tests with the bundled miniz.
- Nothing has run on a K230 yet.
