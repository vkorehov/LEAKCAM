# test027: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-10-31 (run.py, 61 s): pass

2 x 15 fps: ch0 15.0 fps 511 kbit/s, ch1 14.9 fps 509 kbit/s; 1 channel max: ch0 75.3 fps 2516 kbit/s.


### Encoder stall from a full /sdcard, 2026-10-30 (test105)

During test105 the encoder stopped at t~100 s (`ch0: in 16 out 0`, ch1 `in 0`, enqueue failures). Alone, idle board:
the same at t=100 s. `df /sdcard`: 128 KB free; test028's `/sdcard/plt` files had left 12 MB and the two streams
(~60 KB/s each) filled it. venctest wrote up to 30 MB per channel without checking fwrite. Fixes: venctest 4 MB per
channel + stop saving on a short write; test028 removes its files at the end; /sdcard cleaned (20.1 MB free).

### Hang repro, 2026-10-30 (hang_repro.py, [hang_repro.log](hang_repro.log))

29 encodes over start delays 0-24 s, no hang; the lost commands were the console at 4 Mbaud (test039, now 2 Mbaud).

### Automated run 2026-10-30 (run.py, 63 s): pass

2 x 15 fps: ch0 14.8 fps 511 kbit/s, ch1 14.8 fps 510 kbit/s; 1 channel max: ch0 77.7 fps 2592 kbit/s.


### Automated run 2026-10-30 (run.py, 721 s): **FAIL**

2 x 15 fps: ch0 14.8 fps 511 kbit/s, ch1 14.8 fps 510 kbit/s; 1 channel max:  (NOT done).


### Automated run 2026-10-30 (run.py, 61 s): pass

2 x 15 fps: ch0 15.0 fps 511 kbit/s, ch1 15.0 fps 510 kbit/s; 1 channel max: ch0 76.0 fps 2537 kbit/s.


### Automated run 2026-10-25 (run.py, 63 s): pass

2 x 15 fps: ch0 15.0 fps 511 kbit/s, ch1 15.0 fps 510 kbit/s; 1 channel max: ch0 75.9 fps 2535 kbit/s.

The 1-channel top is 10 % below 2026-10-21's 84.2 fps (DDR is faster since, 3200 vs 2667): to explain - one 15 s
run here against one there; repeat several times before calling it a regression (the product needs 2 x 15).

### 2026-10-21

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

### Superseded status line (2026-10-21)

Status: **pass** (2026-10-21): 2 x 15 fps at the product settings, encoder top ~83 fps at 1280x960 (2.8x margin), the stream decodes on the PC.

### Superseded metric values

- 2026-10-21: Test run time: runs 20:44:53-20:46:30; setup 20:33-20:44: write, 2 builds (one failed), flash 33 s, boot.
- 2026-10-21: encoder fps, bitrate: 2 x 15 fps at 510 kbit/s each; top 84 fps (1 ch) / 82 fps (2 ch); product load =
  36 % of the top.

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
