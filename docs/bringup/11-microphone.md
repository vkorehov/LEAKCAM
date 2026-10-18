# 11. Microphone (U13 MSM381ACB026, K230 inner codec)

- **Mic check hung after the codec power-up** -> the AI takes its frames from a VB pool and none existed
  -> `vb_setup()` creates an audio pool (as Canaan's sample_audio).
- **Shell and NAND dead after a mic run; user programs read the NAND at 26 KB/s** (5 MB in 195 s, CPU
  stalls up to 91 ms) -> not the audio and not the supply: RT-Thread's `poll()` busy-looped after any
  wake-up that left the fd not ready (its wake flag was never cleared). The agent's `poll()` on the hook's
  stdout pipe spun at priority 25 from the moment the hook exited, starving every user program and the
  idle thread; the shell's own `cp` (priority 20) stayed fast -> `install.sh` patches `poll.c` (re-arm,
  remaining timeout) and `pipe.c` (POLLHUP when no writer is left). After: 5 MB in 0.41 s (12.7 MB/s),
  CPU stalls 15 us, the agent ends its session.
- **Edits to `spinand_isbusy.c` never reached the build** -> `install.sh` skipped the patch once its
  marker was in `spinand.c` -> the function is now replaced on every run.
- **Clicks, "interrupted" sound; tones at 294/882 Hz** -> the starved system first; then 64-byte lines of
  an older frame in each frame (349 of 399 jumps on 32-sample boundaries): the audio driver leaves parts
  of the frame in the data cache and the non-cached view read DRAM -> `frame_map()`: cached view, flushed
  first. After: no repeated or misplaced 64-byte lines.
- **Recording sounded like white noise** -> normalising a quiet room's floor (-42 dBFS at 54 dB gain) to
  full scale -> judge by levels: claps and speech at the port reach -7 dBFS.
- **Playback stutters** -> the VMware VM's audio, also with a synthetic tone -> listen on the Mac.
- **Getting the samples off the board** -> no network yet -> `-x` dumps the raw file as hex over the console.
- First ~2 s after the codec powers up are clipped (input capacitor charging); the stream skips them.
