# test012: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-11 (run.py, 85 s): pass

3 x 10 s: rms -33.7..-11.6 dBFS


### Automated run 2026-11-11 (run.py, 74 s): **FAIL**

3 x 10 s: rms -33.8..-7.8 dBFS


### Automated run 2026-11-11 (run.py, 75 s): **FAIL**

3 x 10 s: rms -30.1..-8.7 dBFS


### Automated run 2026-10-31 (run.py, 84 s): pass

3 x 10 s: rms -10.6..-4.1 dBFS


### Automated run 2026-10-25 (run.py, 219 s): pass

3 x 10 s: rms -25.3..-2.8 dBFS

- meter K230 at msh idle (agent running): 0.220 A (read in test006-k230-nand-boot-spl)
- meter K230 recording the microphone (leakcam_stream -m 10, repeated): 0.232 A

### First run, 2026-10-18

Status: **pass** (2026-10-18).

Board on USB-C 1 (VBAT 4.25 V), K230 from NAND. `leakcam_stream -m <s> -o /tmp/mic.raw -x` (levels per 0.5 s,
samples as hex over the console). 16 kHz, 16-bit mono, codec left input, PGA 30 dB + ALC 24 dB.
Probe sheet: [mic.png](mic.png).

| Point | Net | Measured (recording running) |
|---|---|---|
| V, C96.1 | mic VDD after FB4 | 1.4 V |
| B, FB4.2 | K230 MIC_BIAS (A4) | 1.4 V |
| O, C97.1 | mic output (DC bias) | 0.8 V |

| Test | Result | |
|---|---|---|
| First run | hung: no VB pool for the AI frames | fixed |
| Recording, NAND afterwards | flat -10 dBFS noise, tones 294/882 Hz; NAND and shell stalled afterwards: kernel `poll()` busy loop | fixed |
| NAND between every audio step (temporary `-n`) | 5 MB read 0.41 s before, during and after audio; CPU stalls <= 0.13 ms | pass |
| Clicks | 349 of 399 jumps on 64-byte boundaries, 60 repeated 64-byte lines: stale cache lines | fixed |
| Quiet room (> 100 Hz) | -42 dBFS (mic self-noise at 54 dB gain) | |
| Claps / speech at the port | up to -7 dBFS, 35 dB over the floor; claps clip at 54 dB; no dropouts, no repeated lines, no seams | pass |
| Codec power-up | first ~2 s clipped (input capacitor charging) | expected |
| Playback | stutters in the VM (also a synthetic tone); clean on the Mac | pass |

**Pass.** Recording: [mic_at_port_2026-10-18.wav](mic_at_port_2026-10-18.wav) (10 s after settling, 100 Hz
high-pass, normalised). Gain stays 54 dB for now (PGA 20 dB would give 10 dB headroom). MIC_BIAS 1.4 V is below
the mic's 1.8 V characterisation point; sensitivity is fine.

#### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

#### Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: extra current with the codec and mic recording vs idle (mA, per second of clip).

#### Old section placeholders

- Setup: **MISSING**; the setup is in the introduction above and in the Results.
- Steps: **MISSING**; the steps are the rows of the Results tables.
- Pass: **MISSING**; every Results row carries its pass / fail.

## Obstacles

- **2026-11-11 23:19 / 23:23, FAIL: run 2 "VB is initialized!" / "vb init failed" (two causes, both fixed 2026-11-12).**
  (1) leakcam_stream never destroyed its audio VB pool, so kd_mpi_vb_exit() failed and VB stayed initialized for the
  next program: vb_teardown() destroys the pool first, as Canaan's sample_audio. (2) run 2 (~17-27 s after the boot)
  overlapped the agent's boot capture hook (`leakcam_wake cold`, 23-28 s): run.py calls r.media_quiet() after the
  boot. Also: a failing run now logs its console reason (the first FAIL logged none). Rerun 01:40: pass.
  Cross-program check: tests/test030-camera-capture/vb_cases.py (208 steps, 0 failed after the fixes).

- The Metrics said "48 kHz mono" from 2026-10-25: a fixed string in run.py, not a reading; the recording is 16 kHz
  (leakcam_stream AUDIO_RATE, the WAV header). Fixed 2026-10-30 (test audit).

- **Mic check hung after the codec power-up** -> the AI takes its frames from a VB pool and none existed ->
  `vb_setup()` creates an audio pool (as Canaan's sample_audio).
- **Shell and NAND dead after a mic run; user programs read the NAND at 26 KB/s** (5 MB in 195 s, CPU stalls up to
  91 ms) -> not the audio and not the supply: RT-Thread's `poll()` busy-looped after any wake-up that left the fd not
  ready (its wake flag was never cleared). The agent's `poll()` on the hook's stdout pipe spun at priority 25 from the
  moment the hook exited, starving every user program and the idle thread; the shell's `cp` (priority 20) stayed fast
  -> install.sh patches `poll.c` (re-arm, remaining timeout) and `pipe.c` (POLLHUP when no writer is left). After:
  12.7 MB/s, CPU stalls 15 us, the agent ends its session.
- **Edits to `spinand_isbusy.c` never reached the build** -> install.sh skipped the patch once its marker was in
  `spinand.c` -> the function is now replaced on every run.
- **Clicks, "interrupted" sound** -> 64-byte lines of an older frame in each frame: the audio driver leaves parts of
  the frame in the data cache and the non-cached view read DRAM -> `frame_map()`: cached view, flushed first.
- **Recording sounded like white noise** -> a quiet room's floor normalised to full scale -> judge by levels.
- **Playback stutters** -> the VMware VM's audio (also a synthetic tone) -> listen on the Mac.
- **Getting the samples off the board** -> no network yet -> `-x` dumps the raw file as hex over the console.
