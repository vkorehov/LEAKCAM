# leakcam programs on RT-Smart (k230_rtos_sdk)

| Program | Sources | What it does |
|---|---|---|
| `leakcam_agent` | `../../k230_agent/leakcam_agent.c`, `../../bl616/k230_link.h` | BL616 link, heartbeat, hook, Wi-Fi request (started at boot) |
| `leakcam_wake` | `leakcam_wake.c`, `vicap_cap.c` (MPP VICAP devices 0 and 1, offline mode), `led_rtsmart.c` (`/dev/pwm`), `netclient.c`, core | the wake algorithm (DESIGN.md 5.1): capture, compare with the history, server check, video, history |
| `leakcam_hist` | `leakcam_hist.c`, core | list and rebuild stored frames |
| `leakcam_stream` | `leakcam_stream.c`, `rtmp.c`, `netclient.c`, libopus | video with audio, live: 5 s over RTMP to the server on a leak (`-p host:1935`), below |

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

## leakcam_stream: 5 s live to the server on a leak

`leakcam_stream -p <host>:<port> [-t <seconds>]` (default 5), started by `leakcam_wake` when the
server answers `{"leak":true}`, with the `/v1/check` host and port 1935. Both cameras are
published at once over RTMP (`rtmp.c`) as `rtmp://<host>:1935/leakcam/cam0` and `/cam1`: the
camera's H.265 (Main), starting on an IDR, and the microphone as Opus, both stamped from the
moment the cameras start. Exit 0 when both streams stayed up to the end.
- **Server side: MediaMTX**, the LEAKCAM server's ingest: Enhanced RTMP v2 with H.265 (FourCC
  `hvc1`) and Opus, re-served to phones as HLS or WebRTC. The host test stands in with FFmpeg
  7.1+ (`ffmpeg -listen 1 -i rtmp://0.0.0.0:1935/leakcam/cam0 -c copy cam0.flv`). Classic RTMP
  servers (nginx-rtmp) know neither codec.
- **Phones:** H.265 decodes in hardware on nearly every Android phone of the last years and in
  Chrome for Android; Opus plays everywhere. Serve HLS / fragmented MP4 with both copied.
- **Protocol:** plain RTMP (simple handshake, chunk size 4096), `connect` to app `leakcam` with
  `fourCcList: ["hvc1", "Opus"]`, `createStream`, `publish <cam> live`, then media: video as
  E-RTMP (SequenceStart with the HEVC decoder configuration record built from VPS/SPS/PPS, then
  CodedFramesX), audio as E-RTMP v2 (SequenceStart with the RFC 7845 ID header, then one Opus
  packet per message). `deleteStream` at the end. No reconnect: a clip is 5 s.
- **Bench viewing:** the same push to a PC: MediaMTX (both cameras), or
  `ffplay -listen 1 rtmp://0.0.0.0:1935/leakcam/cam0` (FFmpeg 7.1+) for one, with a long `-t`.
  There is no RTSP server on the board: the SDK's can carry only G.711 audio.
- Pipeline per camera, hardware end to end: VICAP (offline mode) -> ISP -> NV12, bound to its
  H.265 VENC channel with `kd_mpi_sys_bind` (no CPU per frame; order as the SDK's
  `sample_webrtc/mpp_pipeline.c`). The CPU only moves the finished bitstream and runs Opus.
- Budget, set for battery: 500 kbit/s per camera plus 24 kbit/s of audio, about 1.05 Mbit/s for
  both, so a 5 s clip is about 0.65 MB, far inside the 10-25 Mbit/s expected from the BL616 over
  SDIO. Fewer bytes are less Wi-Fi airtime, 15 fps is half the ISP and encoder work. `FPS`,
  `VIDEO_KBPS`, `GOP_S` and `OPUS_KBPS` in leakcam_stream.c: raise them only if a clip from the
  board is too poor to judge. Under 45 of the 70 MiB of MMZ.
- Needs the network: the hook (or whoever starts it) asks the agent for Wi-Fi first.
- Not verifiable from the source (binary MPP libraries), to check on the board: that VICAP drops
  the 45 fps sensor rate to 15 fps in offline mode.

## Microphone (in every stream)

- **Capture.** 16 kHz, 16-bit mono from the codec's left input: U13 MSM381ACB026 on MICPL/MICNL
  through C97/C101. The SDK names that input `KD_I2S_IN_MONO_LEFT_CHANNEL`, "hp input", after the
  EVB, whose on-board mic is on the right input.
- **Encoding.** Opus (bundled libopus 1.5.2, `third_party/opus`, BSD, float build), 24 kbit/s,
  complexity 3, 40 ms packets: wideband (up to 8 kHz) at a third of G.711's rate, a few percent of
  one core. No DTX: it would call a quiet leak silence and drop it.
- **Gain, for quiet leaks.** All of it in front of the ADC, where it lifts the sound over the
  converter's noise: mic PGA 30 dB (its maximum) plus ALC analog gain +24 dB (range -18..28.5).
  That clips near 80 dB SPL: a door slam, never a leak. Set after `kd_mpi_ai_enable`, because the
  first enable resets the gains. The AI is enabled before the cameras, so its ~2 s codec power-up
  is over by the first frame. `MIC_PGA_DB` / `MIC_ALC_DB` in leakcam_stream.c.
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
