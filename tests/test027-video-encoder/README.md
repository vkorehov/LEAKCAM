# test027: H.265 encoder with synthetic frames

Status: **pass** (2026-10-25): 2 x 15 fps at the product settings (511 / 510 kbit/s); 1-channel top 75.9 fps (84.2 fps on
2026-10-21, open), 2-channel top 82.0 fps (2026-10-21); the stream decodes on the PC (2026-10-21).

The heaviest job of a leak session: two H.265 streams, 1280x960, 15 fps, ~500 kbit/s each (DESIGN.md). The
encoder can be fed synthetic frames, so speed and memory can be measured before the cameras arrive.

## Setup

**MISSING**: hookup, firmware / image, and the probes named in Metrics.

## Needs (build features and dependencies)

MPP VENC (H.265), venctest (temporary), VPU clock.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. [venctest/](venctest/venctest.c), a temporary user program (SDK copy: `src/applications/venctest` + `subdirs-y
   += venctest` in apps.mk), built from the SDK examples (sample_venc, smart_ipc's `kd_mpi_venc_send_frame`), no
   product code: 8 generated NV12 frames per channel (scrolling gradient, moving square, noise band) cycled into
   VENC, H.265 Main CBR 500 kbit/s, GOP 150 at 15 fps (the product's settings); `/sdcard/app/venctest [1|2]
   [seconds] [fps, 0 = max]`; stream to /sdcard/venc<N>.h265 (4 MB max, the first ~65 s; a short write stops the saving, not the encoder); per second frames in/out, bytes, I
   frames; at the end fps, kbit/s, IDR spacing, send-to-stream latency.
2. Measure encode time per frame, CPU load, memory (VB pools, heap), the stream's bit rate and its IDR spacing.
3. Play the stream back on the PC (ffprobe/ffplay) to check it decodes.

## Pass

Both channels hold 15 fps with margin, memory leaves room for the change net and the network stack, the stream
decodes and keeps the rate and IDR interval.

## Result

- **15 fps holds on both channels with the product settings** (H.265 Main CBR 500 kbit/s, GOP 150): ch0 15.0 fps
  511 kbit/s, ch1 15.0 fps 510 kbit/s (2026-10-25, run.py); the CBR keeps 500 kbit/s within 2 %, IDR every 149-150
  frames, send-to-stream 10 ms avg / 38 ms max on one channel (2026-10-21).
- The encoder is cheap: +2 mA for one channel, +15 mA for two (x 4.15 V: +8 / +62 mW); no measurable heat in 20 s
  (2026-10-21).
- Encoder top at 1280x960 H.265: 1 channel **75.9 fps** (2535 kbit/s, 2026-10-25, run.py); 2 channels 41.3 + 40.7 =
  **82.0 fps**, latency 17-18 ms avg / 175-205 ms max, 0 send failures (2026-10-21). Bit rate scales with the frame
  rate at max rate because CBR is set for 15 fps.
- `kd_mpi_venc_send_frame(..., -1)` does not block when the encoder's input queue is full, it fails at once
  ("venc_input_enqueue>failed"): the feed keeps at most 3 frames in flight and backs off 2 ms after a failed send
  (2026-10-21).
- The bit rate starts high (363 KB in the first second: the first IDR and CBR settling), then 22-90 KB/s (2026-10-21).
- Decode check: `venctest 1 1 15` -> [venc0_1s.h265](venc0_1s.h265) (367,574 B, 1 IDR + 14 P); ffprobe: hevc Main,
  1280x960, yuv420p, 16 frames; `ffmpeg -f null` decodes with no error; frames 0 and 14
  ([decoded_frames_0_14.jpg](decoded_frames_0_14.jpg)) show the input pattern at its position: pass (2026-10-21).

- A full /sdcard stalls the encoder (2026-10-30): both channels ran 15/15 fps until t=100 s, then `out 0 fps` with
  `venc_input_enqueue failed / sys queue no space` (1085 times in 300 s), with no other load. test028's power-cut files
  had left 12 MB free and the two streams filled it; venctest never checked fwrite, so every write stalled the drain
  thread and the input queue filled. Fixed in venctest: at most 4 MB per channel (~65 s), and a short write prints
  `disk full`, closes the file and keeps encoding. Product rule: the clip writer checks every write and space first.
- The earlier "hang" of 2026-10-30 (hang_repro.py, 29 encodes over 13 start delays: no hang) was the console losing
  commands at 4 Mbaud (test039), not the encoder.

Open:
- The 1-channel top is 10 % below 2026-10-21's 84.2 fps although DDR is faster since (3200 vs 2667): one 15 s run each;
  repeat several times before calling it a regression (the product needs 2 x 15).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | pt_run -> msh 0.47 / 0.47 s (median / max of 1; SPL 0.16, OpenSBI 0.24, RT-Smart 0.25, mounted 0.46, msh 0.47); longest step to mounted 0.21 s (2026-10-25, run.py) | target (test022) | longest step: |
| This part's boot share | n/a: the encoder starts in the program (2026-10-25) | - | |
| Test run time | 63 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| encoder fps, bitrate | 2 x 15 fps at 511 / 510 kbit/s (2026-10-25); top 75.9 fps 1 ch (2026-10-25) / 82 fps 2 ch (2026-10-21) | VPU 800 MHz | product load 2 x 15 = 30 fps |
| Current idle / during | 205 / 207 (1 ch) / 220 mA, peaks 245 (2 ch) / 270 mA at max rate | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT ~4.15 V |
| Voltage under load (min) | VBAT 4.36 V min (K230 ADC ch 1, test023 calibration, 3 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | 2 channels: +62 mW over idle; a 5 s two-camera clip +0.31 J (21 uAh) for the encoder alone | - | by hand |
| Per day | 4 alarm clips a day x 20 s of 2 x 15 fps: see ENERGY.txt (the encoder adds ~15 mA, 2026-10-21) (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | 47.9 / 48.5 / 47.9 C (board not read) | K230 limit (datasheet) | encoder adds no measurable heat in 20 s |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 206440 / min 204396 / 206440 B, blocks 16 -> 16, deleted 0; K230 heap available 7025152 / 7025152 / 7312064 B; leak: n/a, 3 snapshots (mem_leak.py needs >= 8 cycles) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | kdimg 77.8 MB (/sdcard 176 MiB, /bin 20 MiB layout), kernel ~3.7 MB | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, -ffunction/-fdata-sections, --gc-sections, strip (no LTO) | smallest code (rule) | |
| Features over the minimal image | venctest 79 600 B on /sdcard (test only); the encoder (MPP) is in the product's leakcam_stream (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (the VPU encodes; the CPU only queues frames): compiler flags do not change the result (2026-10-25) | the faster one stays | |

## Boot time

Encoder / media stack init time (VB pools, VENC) at boot or at first use.

## Battery energy

Battery current with one and two encoder channels running; energy per second of clip (two cameras, 15 fps).
