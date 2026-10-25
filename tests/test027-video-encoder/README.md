# test027: H.265 encoder with synthetic frames

Status: **pass** (2026-10-21): 2 x 15 fps at the product settings, encoder top ~83 fps at 1280x960 (2.8x margin), the stream decodes on the PC.

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
   [seconds] [fps, 0 = max]`; stream to /sdcard/venc<N>.h265 (30 MB max); per second frames in/out, bytes, I
   frames; at the end fps, kbit/s, IDR spacing, send-to-stream latency.
2. Measure encode time per frame, CPU load, memory (VB pools, heap), the stream's bit rate and its IDR spacing.
3. Play the stream back on the PC (ffprobe/ffplay) to check it decodes.

## Pass

Both channels hold 15 fps with margin, memory leaves room for the change net and the network stack, the stream
decodes and keeps the rate and IDR interval.

## Results (2026-10-21, test image flashed 20:42:38-20:43:11, NAND boot 20:44:36)

Console logs: [run1.log](run1.log), [run2.log](run2.log), [run3.log](run3.log) (the 1586 enqueue-failure lines of run 3 left out).

| Run | Time | Result | Meter (USB-C 1) | Die |
|---|---|---|---|---|
| idle (msh) | 20:44 | - | 205 mA | 47.9 C |
| 1: 1 channel, 15 fps, 20 s | 20:44:53-20:45:15 | 15.0 fps, 510 kbit/s, IDR every 150 frames, send-to-stream 10 ms avg / 38 ms max | 207 mA | 48.5 C after |
| 2: 2 channels, 15 fps, 20 s | 20:45:24-20:45:47 | 15.0 / 14.9 fps, 511 / 508 kbit/s, IDR every 149; SDK: "Average FrameRate = 15 Fps" each | 220 mA, peaks 240-245 | 47.9 C after |
| idle | after run 2 | - | 205 mA | |
| 3: 2 channels, as fast as possible, 15 s | 20:46:00-20:46:30 | **hung**: send_frame queue full ("venc_input_enqueue>failed" x 1586), channel 1 no output, program never ended; K230 power-cycled 20:47 | 220 mA, no peak | - |
| after the power cycle | 20:47 | - | 217 mA (booting) | |
| 4: 1 channel, max rate (3 in flight), 15 s | 20:50:15-20:50:30 | **84.2 fps**, 0 send failures, IDR every 149-150, latency 20 ms avg / 82 max | 270 mA (during 4-5, not split) | |
| 5: 2 channels, max rate, 15 s | 20:50:30-20:50:46 | **41.3 + 40.7 = 82.0 fps**, latency 17-18 ms avg / 175-205 max | | |

- **15 fps holds on both channels with the product settings** and the CBR keeps 500 kbit/s within 2 %; the encoder is
  cheap: +2 mA for one channel, +15 mA for two (x 4.15 V: +8 / +62 mW).
- **Encoder top: ~83 fps at 1280x960 H.265**, shared by the channels (84 for one, 82 for two): the product's 2 x 15 =
  30 fps is 36 % of it (2.8x margin). Runs 4-5 with the fixed venctest (logs run4/run5); bit rate scales with the
  frame rate there because CBR is set for 15 fps.
- The bit rate starts high (363 KB in the first second: the first IDR and CBR settling), then 22-90 KB/s.

**Decode check (20:51-21:12):** `venctest 1 1 15` -> /sdcard/venc0.h265, 367,574 B (1 s: 1 IDR + 14 P), taken off the
board with msh `hexdump` over the console (~11 KB/s, 2.5 min per dump) and rebuilt with
[tools/k230-console/unhexdump.py](../../tools/k230-console/unhexdump.py) from two dumps (22,825 and 22,956 of 22,974
lines good, 0 disagreeing, 0 missing in both): [venc0_1s.h265](venc0_1s.h265). ffprobe: hevc Main, 1280x960, yuv420p,
16 frames; `ffmpeg -f null` decodes with no error. Frames 0 and 14 ([decoded_frames_0_14.jpg](decoded_frames_0_14.jpg)):
the generated gradient, chroma columns, noise band, and the square at the input pattern's position (frame 14 =
pattern 6, x 240 y 150). **Pass.**

### Automated run 2026-10-25 (run.py, 63 s): pass

2 x 15 fps: ch0 15.0 fps 511 kbit/s, ch1 15.0 fps 510 kbit/s; 1 channel max: ch0 75.9 fps 2535 kbit/s.

The 1-channel top is 10 % below 2026-10-21's 84.2 fps (DDR is faster since, 3200 vs 2667): to explain - one 15 s
run here against one there; repeat several times before calling it a regression (the product needs 2 x 15).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | pt_run -> msh 0.47 / 0.47 s (median / max of 1; SPL 0.16, OpenSBI 0.24, RT-Smart 0.25, mounted 0.46, msh 0.47); longest step to mounted 0.21 s (2026-10-25, run.py) | target (test022) | longest step: |
| This part's boot share | n/a: the encoder starts in the program (2026-10-25) | - | |
| Test run time | runs 20:44:53-20:46:30 | - | setup 20:33-20:44: write, 2 builds (one failed), flash 33 s, boot |
| encoder fps, bitrate | 2 x 15 fps at 510 kbit/s each; top 84 fps (1 ch) / 82 fps (2 ch) | VPU 800 MHz | product load = 36 % of the top |
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

## Obstacles

- **First build failed: `k_venc_attr` has no `stream_buf_size` / `stream_buf_cnt`** (my mistake: fields assumed, not
  read from this SDK's k_venc_comm.h) -> removed; the stream buffers come from the attached VB pool.
- **Run 3 (max rate) hung the K230 shell** (my bug) -> `kd_mpi_venc_send_frame(..., -1)` does not block when the
  encoder's input queue is full, it fails at once ("venc_input_enqueue>failed"); the feed loop spun on it, flooded the
  console and channel 1 never produced a frame -> fix: in max mode keep at most 3 frames in flight (feed waits for the
  drain), back off 2 ms after a failed send; latency by pts. Redone as runs 4-5: 0 failures.
- **"vicap: video buffer pools failed (MMZ too small?) / capture failed" during run 1** -> the agent's test hook
  tried to capture while venctest held the VB pools; not from venctest, expected while it runs.
- **Run 2's latency (110 ms avg) is not a real figure** -> latency pairs frames by count; one dropped frame shifts all
  later ones by 67 ms -> use per-frame pts in the redo.
- **Console dumps lose lines** (msh `hexdump`, 2026-10-21) -> the first dump had a stale reader for its first
  seconds (my mistake again: the `ls` step's `cat` still open), the second ran while the first dump's 330 s reader
  was still open (unusable); a clean single-reader dump still drops ~16 lines in 23k and can garble one into a line
  that parses -> unhexdump.py checks each line's ASCII column and merges dumps; always list and kill earlier readers
  (by PID) before a capture.

None in the run of 2026-10-25.
