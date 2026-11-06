# test045: link frames reach a K230 program through /dev/link (low level: the BL616 <-> K230 link, both ways)

Status: **pass** (2026-11-05, after the kernel fix below): 5/5 K230 starts, the program got ACK + WAKE setup every
time; READY 0.43 s after pt_run, READY -> WAKE 8 ms inside the program; `/dev/link fops attached`.

The product's K230 program (k230_app/leakcam.c) starts by sending READY over the link and waits for the BL616's ACK and
WAKE `<reason>`, which picks its job (DESIGN.md 5.3.8). Before this test nothing had ever proven that a K230 *program*
receives a frame: test039 proved the console over the link and the kernel's frame split, test010/018 the wire, but the
old test agent's READY was never answered either, and nobody looked. This test proves the whole path: BL616 frame ->
UART1 -> link_mux -> `/dev/link` -> `poll()` + `read()` in a user program, and the program's answer back.

## Root cause found by this test (2026-11-05)

`link_mux_init()` set `link.fops = &link_fops` **before** `rt_device_register(&link, "link", ...)`, and
`rt_device_register()` sets `dev->fops = RT_NULL` and re-inits the wait queue (rt-thread/src/device.c, under
`RT_USING_POSIX`). So `/dev/link` opened with devfs's generic file ops: its `poll()` (`dfs_device_fs_poll`) always
returns 0, so the program never read a frame, while a plain `read()` (through `rt_device_read`) did return the bytes.
Fix: set `fops` after the register (firmware/k230_board/rtsmart/drivers/link_mux/link_mux.c). The serial driver in the
SDK does it in that order. The chase, with the wrong turns: HISTORY.md.

`link_stat` (K230 msh) now shows it every time: `/dev/link fops attached|MISSING, waiting <B>, opens, polls (data),
reads (<B>)`.

## Setup

- K230: the product image (`firmware/k230_board/install.sh`, AUTO_EXEC `/sdcard/app/leakcam &`) with link_mux.
- BL616: the test firmware tests/firmware/bl616_powertest (K230 power, `pt_uart on` = the K230 console bridge; frames
  from the K230 show as `link rx:` on the BL616 shell; `pt_uart frame <body>` sends one frame with its CRC and `\n`,
  never `\r` - `pt_uart send` appends `\r\n` and the splitter rejects it).
- The runner answers each READY within ~5 ms of seeing it: `pt_uart frame <seq>,ACK,<seq>`, then
  `pt_uart frame 0,WAKE,setup,0,-1,0,-1,-1` - the setup firmware's k230_session.c answers the same way.
  By hand is too slow: the program gives up after 3 READY calls x 5 sends x 300 ms = 4.5 s.

## Needs (build features and dependencies)

K230 kernel link_mux (UART1 shared console + link, test039), the product program; BL616 test firmware with the link
bridge. Nothing else.

## Steps

`run.py`, 5 times: K230 off -> on -> pt_run; answer READY with ACK + WAKE setup; the program must log
`leakcam: WAKE setup: t <ms>`. Then `link_stat`: fops attached, poll saw data, no frame dropped; memory sampled.

## Pass

WAKE setup in the program after every start (5/5); `/dev/link fops attached`; polls with data > 0; frame_drop 0.

## Result

2026-11-05, run.py on the fixed image, 4/4 checks pass (31 s): 5/5 starts READY 0.43 s after pt_run and WAKE setup
logged by the program 0.44 s after it (READY -> WAKE 8 ms, the host's answer included); link_stat
`/dev/link fops attached, waiting 0 B, opens 1, polls 6 (data 2), reads 42`, frames 2 frame_drop 0.

Before the fix (2026-11-05, same answers within 5 ms): the K230 kernel took all
30 frames (0 dropped, 0 UART errors), the program read none: `polls 0`, `fops 0x0`, 614 B waiting in the ring,
"READY not answered" x3, "FATAL: no WAKE from the BL616".

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median / max of 5) | pt_run -> first READY 0.43 s (median of 5, max 0.43 s) | target (test022) | pt_run -> the program's first READY on the BL616 |
| This part's boot share | n/a: the link answer is not a boot step | - | |
| Test run time | 31 s automated (run.py, 2026-11-05), no setup | - | |
| READY -> WAKE in the program | READY -> WAKE in the program 8 ms (median of 5, max 8 ms); WAKE 0.44 s after pt_run | one round trip: < 10 ms (frames 0.1 ms at 2 Mbaud) | the program's own step times (start -> WAKE) include the host's answer |
| Current idle / during (min / avg / peak) | n/a: not metered (a link test, seconds per start) | floor (test031) | |
| Voltage under load (min) | n/a: not metered | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: not metered | - | |
| Per day | n/a: one READY per wake, counted in the wake's energy | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | die 49.7 / 49.7 / 49.7 C; board 41.4 / 41.4 / 41.4 C (AHT20) (2026-11-05) | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 166368 / min 162156 / 166368 B, blocks 14 -> 14, deleted 0; K230 heap available 7007360 B steady; leak: n/a, 1 snapshots (mem_leak.py needs >= 8 cycles) (2026-11-05) | no leak | |
| Image (sizes) | BL616 test firmware 1322112 B (2026-11-05, on the board at the run) | minimal image (rule) | |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -Os -g (SDK Kconfig default), no LTO (2026-11-05) | smallest code (rule) | |
| Features over the minimal image | the product image (CherryUSB device + CDC ACM, leakcam program) | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

pt_run -> the program's first READY, seen on the BL616 shell (Boot row).

## Battery energy

n/a: a link test; the READY round trip is part of every wake (test022's budget).
