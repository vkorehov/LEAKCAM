# test026: H.265 encoder with synthetic frames

Status: **pass at 15 fps, 1 and 2 channels** (2026-10-21); the max-rate run hung (venctest bug), to redo; the decode check on the PC is open.

The heaviest job of a leak session: two H.265 streams, 1280x960, 15 fps, ~500 kbit/s each (DESIGN.md). The
encoder can be fed synthetic frames, so speed and memory can be measured before the cameras arrive.

## Steps

1. [venctest/](venctest/venctest.c), a temporary user program (SDK copy: `src/applications/venctest` + `subdirs-y
   += venctest` in apps.mk), built from the SDK examples (sample_venc, smart_ipc's `kd_mpi_venc_send_frame`), no
   product code: 8 generated NV12 frames per channel (scrolling gradient, moving square, noise band) cycled into
   VENC, H.265 Main CBR 500 kbit/s, GOP 150 at 15 fps (the product's settings); `/sdcard/app/venctest [1|2]
   [seconds] [fps, 0 = max]`; stream to /sdcard/venc<N>.h265 (30 MB max); per second frames in/out, bytes, I
   frames; at the end fps, kbit/s, IDR spacing, send-to-stream latency.
2. Measure encode time per frame, CPU load, memory (VB pools, heap), the stream's bit rate and its IDR spacing.
3. Play the stream back on the PC (ffprobe/ffplay) to check it decodes.

## Pass

Both channels hold 15 fps with margin, memory leaves room for the change net and the network stack, the stream
decodes and keeps the rate and IDR interval.


## Result (2026-10-21, test image flashed 20:42:38-20:43:11, NAND boot 20:44:36)

Console logs: [run1.log](run1.log), [run2.log](run2.log), [run3.log](run3.log) (the 1586 enqueue-failure lines of run 3 left out).

| Run | Time | Result | Meter (USB-C 1) | Die |
|---|---|---|---|---|
| idle (msh) | 20:44 | - | 205 mA | 47.9 C |
| 1: 1 channel, 15 fps, 20 s | 20:44:53-20:45:15 | 15.0 fps, 510 kbit/s, IDR every 150 frames, send-to-stream 10 ms avg / 38 ms max | 207 mA | 48.5 C after |
| 2: 2 channels, 15 fps, 20 s | 20:45:24-20:45:47 | 15.0 / 14.9 fps, 511 / 508 kbit/s, IDR every 149; SDK: "Average FrameRate = 15 Fps" each | 220 mA, peaks 240-245 | 47.9 C after |
| idle | after run 2 | - | 205 mA | |
| 3: 2 channels, as fast as possible, 15 s | 20:46:00-20:46:30 | **hung**: send_frame queue full ("venc_input_enqueue>failed" x 1586), channel 1 no output, program never ended; K230 power-cycled 20:47 | 220 mA, no peak | - |
| after the power cycle | 20:47 | - | 217 mA (booting) | |

- **15 fps holds on both channels with the product settings** and the CBR keeps 500 kbit/s within 2 %; the encoder is
  cheap: +2 mA for one channel, +15 mA for two (x 4.15 V: +8 / +62 mW).
- Run 1's latency (10 ms per frame at 1280x960) says one channel is far from the encoder's limit; the real top waits
  for run 3 redone.
- The bit rate starts high (363 KB in the first second: the first IDR and CBR settling), then 22-90 KB/s.

## Metrics (to fill on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | runs 20:44:53-20:46:30 | - | setup 20:33-20:44: write, 2 builds (one failed), flash 33 s, boot |
| encoder fps, bitrate | 2 x 15 fps at 510 kbit/s; top not measured (run 3 hung) | VPU 800 MHz | 10 ms per frame (1 channel) |
| Current idle / during | 205 / 207 (1 ch) / 220 mA, peaks 245 (2 ch) | floor (test030) | USB-C 1 VBUS meter, D1 off; VBAT ~4.15 V |
| Power, energy per operation | 2 channels: +62 mW over idle; a 5 s two-camera clip +0.31 J (21 uAh) for the encoder alone | - | by hand |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | 47.9 / 48.5 / 47.9 C (board not read) | K230 limit (datasheet) | encoder adds no measurable heat in 20 s |

## Boot time

Encoder / media stack init time (VB pools, VENC) at boot or at first use.

## Battery energy

Battery current with one and two encoder channels running; energy per second of clip (two cameras, 15 fps).

## Obstacles

- **First build failed: `k_venc_attr` has no `stream_buf_size` / `stream_buf_cnt`** (my mistake: fields assumed, not
  read from this SDK's k_venc_comm.h) -> removed; the stream buffers come from the attached VB pool.
- **Run 3 (max rate) hung the K230 shell** (my bug) -> `kd_mpi_venc_send_frame(..., -1)` does not block when the
  encoder's input queue is full, it fails at once ("venc_input_enqueue>failed"); the feed loop spun on it, flooded the
  console and channel 1 never produced a frame -> fix: in max mode keep at most 3 frames in flight (feed waits for the
  drain), back off 2 ms after a failed send; redo run 3.
- **"vicap: video buffer pools failed (MMZ too small?) / capture failed" during run 1** -> the agent's test hook
  tried to capture while venctest held the VB pools; not from venctest, expected while it runs.
- **Run 2's latency (110 ms avg) is not a real figure** -> latency pairs frames by count; one dropped frame shifts all
  later ones by 67 ms -> use per-frame pts in the redo.

