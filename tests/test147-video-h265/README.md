# test147: Take video -> two native H.265 videos at the server, live while they are made (product pair)

Status: **both cameras native at 15 fps (2026-11-12 22:30 UTC+2): 225 frames each, both MP4s decode; the upload of
two 15 fps streams is at the bridge's limit** (one run in two: lwIP "Out of memory", uploads aborted). The cause of
7.5 fps was VICAP's 2 raw buffers per camera; 3 fit for both cameras only with the raw frames 10-bit packed
(k230_app/rawpack.c: link-time patch of the closed ISP library; firmware/VIDEO.txt has everything). Open: the upload
(lwIP memory or a lower bitrate), 3 passing rounds in a row.

The app's Take video and a leak's videos (DESIGN.md 5.3.14; user 2026-11-12: "h265 with native resolution 15
frames", "streaming server to be golang, same server", "just add lib"): per camera 15 s at 2592x1936, 15 fps, H.265 on
the K230's VPU, sent to the LEAKCAM server as it is encoded; the server serves it live (HLS) and writes the MP4.

## How it works

- The check-in answer's `capture` kind `video` (IDs `<device>-<unix s>-v1`, `-v2`, `secs` 15) -> `SHOOT` -> the K230
  (media.c): the Wi-Fi bridge up, per camera an H.265 channel (h265.c: VENC 1 + cam, Main, CBR 4 Mbit/s, an IDR every
  second) and a chunked `POST /v1/media/<id>` (Content-Type `video/x-leakcam-h265`: per frame [u32 BE length][access
  unit, Annex-B], headers X-Fps, X-Taken), one uploader thread each.
- The frames: the job's shot (the room's light or the LEDs, job.c), then 224 more per camera from the running native
  streams (vicap_cap.c cap_stream, no copy, no stream restart), each sent to the encoder and queued for the upload.
- The server (server/video.go, gohlslib + mediacommon): live fMP4 HLS at `/v1/live/<id>/index.m3u8` while the upload
  runs (from its 2nd whole segment, ~2 s), then the MP4 (hvc1, creation time X-Taken) at `/v1/media/<id>`.

## Setup

Board on the bench, USB-C 1 in (USB power), full firmware, the server deployed (server/deploy.sh), ffprobe/ffmpeg on
this host. Server side alone: `cd server && go test -run TestVideo` (a real libx265 clip, testdata/clip.h265).

## Steps

1. Ask the server for a video, `presence now`; poll the live playlist of camera 1 while the board records.
2. Wait for the BL616's `capture: SENT` (within 90 s); the K230's `media: v<n>` lines (frames, IDRs, bytes, kbit/s).
3. Fetch both MP4s: ffprobe HEVC 2592x1936, 15/1, 225 frames, every frame decodes, creation time = X-Taken.
4. Three check-ins after: all reach the server (the bridge must not outlive the capture).

## Pass

Every check of run.py.

## Result

- Both videos: 115 frames (8 IDR), 1.9 MB each over 15.3 / 16.0 s; the K230's part: encode 3.3 s in all (29 ms a
  frame pair), waiting for camera frames 11.9 s, sending 0.12 s, the upload backlog at most 0.12 MB; uploaded 0.8 /
  1.2 s after the last frame; live playlist from 25 s after the ask (the session start, the bridge, 2 segments).
- **7.5 fps**: `vicap: dev N video: 114 frames over 15065 ms of pts` on both cameras; test030's 1280x960 top (32.7 fps
  each) is the same ~40 MP/s per camera. The ISP clock register (CMU 0x91100068 = 0x00820835): PLL0/4, divider 1/1.
- Obstacles on the way: HISTORY.md.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | **MISSING** | test022 | |
| This part's boot share | n/a: the video is not at boot | - | |
| Test run time | 34 s automated (run.py, 2026-11-12), no setup | - | |
| Speed / rate against the top | **MISSING**: fps reached per camera, kbit/s | 15 fps (the sensor mode) | |
| Current idle / during | **MISSING** | floor (test031) | |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | **MISSING**: energy per 15 s video | - | |
| Per day | n/a: on request or a leak | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak | |
| Image (sizes) | BL616 test firmware 1426720 B (2026-11-12, on the board at the run) | minimal image (rule) | |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -Os -g (SDK Kconfig default), no LTO (2026-11-12) | smallest code (rule) | |
| Features over the minimal image | h265.c (VENC H.265), chunked HTTP | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |
