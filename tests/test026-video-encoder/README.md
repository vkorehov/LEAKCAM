# test026: H.265 encoder with synthetic frames

Status: **in progress** (2026-10-21): venctest written, test image building.

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


## Metrics (to fill on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| encoder fps, bitrate | not measured yet | VPU 800 MHz | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |

## Boot time

Encoder / media stack init time (VB pools, VENC) at boot or at first use.

## Battery energy

Battery current with one and two encoder channels running; energy per second of clip (two cameras, 15 fps).

## Obstacles

- **First build failed: `k_venc_attr` has no `stream_buf_size` / `stream_buf_cnt`** (my mistake: fields assumed, not
  read from this SDK's k_venc_comm.h) -> removed; the stream buffers come from the attached VB pool.
