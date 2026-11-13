# test148: the video's worst cases for the encoder and the upload (product pair)

Status: **pass (2026-11-13 06:25 UTC)**: all four light patterns, both cameras 15 fps at 10 Mbit/s, every frame
decodes, 3 check-ins after; after three fixes below (the BL616's check-ins, the K230's memory split, frame drops on a
full upload queue).

User 2026-11-13: "test quick change of frame by simulating lighting change ... test worse cases of encoding". Test147's
Take video (both cameras, 2592x1936, 15 fps, H.265 CBR 10 Mbit/s) while the host switches both LED chains at full
current through the K230's msh (`pwm_set pwm <ch> 40000 <ns>`, test009) in a pattern:

| Pattern | What changes | Brightness step per frame (mean luma, 0-255) |
|---|---|---|
| none | the room's light only (the first frame is the job's LED shot) | 0.05-0.3 |
| frame | on/off every 67 ms: every frame, edges across frames (rolling shutter) | 28-45 |
| step | on/off every 2 s: exposure steps between still frames | short jumps |
| random | on/off at random 30-300 ms | 23-30 |

`run.py [pattern ...]` (default all four). Per video: frames, fps, every frame decodes (ffmpeg), each frame's size
(IDR / P, the largest against the encoder's 1.25 MB stream buffer), bit rate (average, peak second); the K230's page
pool (`list_page` every second), its kernel heap (`list_memheap`), "Memory exhaustion"; the BL616's heap and lwIP
(`mem`, with its TCP connections) before, after the videos and after each of 3 check-ins.

## Result

- Encoder: every video decodes in every pattern, 225 frames. Light changes do not break the stream: the largest frame
  888 KB (an IDR, frame pattern), P frames up to 782 KB at a light step (= an IDR's size), all under the 1.25 MB
  stream buffer (h265.c). CBR holds the average (9.2-10.5 Mbit/s) but one second peaks at up to 15.7 Mbit/s
  (frame, random; reported, not judged: no run-time rate control in the library); with steps it under-spends (7.0
  Mbit/s average on J5). Encode time does not grow (6.4-6.9 s per 225 frames).
- The lights run from the ask to SENT: the K230's console reaches the host late in bursts during the stream, a trigger
  on its "media: video" line started them 1.5 s before the end.
- Frames: the cameras drop 0-2 of 225 (VICAP sequence numbers, mostly camera 0 / J4); a video's first frame is the
  job's LED shot, up to 0.6 s before the stream's first (J4, frame pattern: 14.49 fps over the MP4).
- **Fixed: the BL616's check-ins after a video failed** (0x15, 0x12; 3 in a row = the setup firmware), every run
  before the fix. Cause, found with the new `mem` lwIP lines: each check-in's own mTLS connection stayed in LAST_ACK
  with its FIN and close_notify unacked (the server closes first; the BL616 leaves the network right after), ~2.4 KB
  of its 6 KB lwIP heap for a minute or more; two such and the next TLS hello found no memory (`tls: send 225 B:
  errno 11`), its failure left another. bl616_common/wifi_check.c `leave()` now aborts the BL616's own TCP
  connections (RST while still joined). After: lwIP heap back to 60 B after the videos and every check-in, 0x03 x3.
- **Fixed: the K230's memory during two-camera video** (user: "change split of cause. We need two parallel videos
  from both cameras streaming"). Before (52 MB RT-Smart / 76 MB MMZ): page pool free 3.4-4.6 MB at the lowest, the
  kernel heap 478 KB; once a channel took 1.4 MB more pages and the pool ran out ("Memory exhaustion!", encoding at 75
  ms a frame, both videos lost). The split is now 55 / 73 MB with a 9 MB kernel heap (k230d_rtos_leakcam_defconfig;
  MMZ's peak during a video is 72.5 MB, fixed sizes): page pool free 4.7-6.8 MB at the lowest, kernel heap 1.4-1.5 MB.
- **Fixed: the upload queue against a slow link.** media.c VIDEO_QUEUE was 12 MB a camera, more than the page pool
  holds; now 2.5 MB (~2 s at 10 Mbit/s; normal backlog peaks 1.0-1.7 MB). A full queue drops that camera's frames up to
  the next IDR (at most 1 s) instead of failing the video: a leak capture during these runs met a Wi-Fi dip (both
  backlogs at 2.6 MB) and lost both videos before this. Proved with a temporary 512 KB build: 23-59 frames dropped in
  2-4 gaps per video, every frame decodes, each gap ends on a keyframe, the MP4 keeps the real time (gaps ~1 s).
- Separate K230 image for video (user's question): not now; the memory goes to what video needs anyway (the cameras'
  67.5 MB of MMZ, the encoders' 24 MB of pages); worth it when the change net's model needs memory beside the encoders.
- **Audio (2026-11-13 06:57 UTC):** each video carries the microphone (k230_app/audio.c: 16 kHz mono, PGA 30 + ALC 24
  dB, Opus 24 kbit/s in 40 ms packets, as tests/test012), 374-375 packets per 15 s video, none lost; the MP4's Opus
  track starts 0.12-0.16 s after the first picture and runs to its end; every frame decodes. The camera times and the
  microphone's are the same clock (CLOCK_MONOTONIC in us: first picture 6845693 us at monotonic 6969986). Level
  -11..-13 dBFS a second, peaks 0 dBFS: music from a laptop beside the board; the user listened: "audio is good"
  ([mic_during_video_2026-11-13.wav](mic_during_video_2026-11-13.wav), camera J4's track).
- Bench: light switching from the console works during a video (msh answers between the app's lines).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | **MISSING** | test022 | |
| This part's boot share | n/a: the video is not at boot | - | |
| Test run time | 34 s automated (run.py, 2026-11-12), no setup | - | |
| Speed / rate against the top | **MISSING**: fps, kbit/s, largest frame per pattern | 15 fps, 10 Mbit/s CBR | |
| Current idle / during | **MISSING** | floor (test031) | |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | **MISSING**: energy per 15 s video | - | |
| Per day | n/a: on request or a leak | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | BL616 heap 155 / 126 / 155 KB, lwIP 60 / - / 60 B (after the fix); K230 page pool free min 3.4 MB of ~38, kernel heap 478 KB left of 8 MB (2026-11-13) | no leak | the K230's margins: Result |
| Image (sizes) | BL616 test firmware 1426720 B (2026-11-12, on the board at the run) | minimal image (rule) | |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -Os -g (SDK Kconfig default), no LTO (2026-11-12) | smallest code (rule) | |
| Features over the minimal image | h265.c (VENC H.265), chunked HTTP | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |
