# test045: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-05 (run.py, 31 s): pass

4/4 checks pass.


### 2026-11-05 diagnostic run (scratch runner, before the fix): FAIL, root cause found

DIAG build: link_stat counted the driver's poll/read calls and printed `link.fops`; the program, on every poll
timeout, logged poll's result and tried one `read()` anyway. Answers within 5 ms of each READY:

- program: `poll rc 0 revents 0x0` every time, but `read 1` returned the frame bytes one by one (`$`, `0`, `,`, `A`,
  `C`, `K` ...): the data was there, poll did not report it;
- kernel: `ring 614` bytes waiting, `opens 0 polls 0 reads 0`, **`fops 0x0`**: the driver's file ops were never used.

`rt_device_register()` clears `dev->fops` (rt-thread/src/device.c 64-67); link_mux set it before the register. Fix:
after it. devfs then took its generic ops (`dfs_device_fs_poll` returns 0 always; read through `rt_device_read`).

### 2026-11-05 automatic answers (scratch runner, before the diagnostic build): FAIL

Each READY answered within 5 ms (`pt_uart frame` from a host script watching the BL616 shell): the kernel took all 30
frames (`link_stat: frames 30 frame_drop 0`, 0 UART errors), the program still logged "READY not answered" x3 and
"FATAL: no WAKE from the BL616". This ruled out the BL616, the wire and the kernel's split.

## Obstacles

- **The driver's file ops were wiped by rt_device_register()** (root cause, 2026-11-05). RT-Thread's register resets
  `fops` and the wait queue; set both after it. Any RT-Smart driver with its own `dfs_file_ops` has the same trap:
  check `fops` in a stat command (link_stat now prints `fops attached|MISSING`). Rule in skill leakcam-product-build
  section 11.
- **Nothing had ever proven a K230 program receives a frame.** The old test agent (tests/firmware/k230_apps/
  leakcam_agent.c) had the same read path and its READY was never answered either; test039 proved the console and the
  kernel's frame split only, its counters (`frames N`) looked like success. Lesson: a link test must prove the
  *receiver's* program got the message, not that a lower layer counted it.
- **Hand answers prove nothing against a 4.5 s window.** The first check answered READY by typing `pt_uart frame` in
  the BL616 shell: the frames arrived (`frames 2`) after the program had already given up, which looked like "the
  kernel has the frames but the program ignores them" - true by accident. Answer from a script (run.py).
- **Wrong hypotheses on the way (2026-11-05), each cost a build or a flash:** the BL616 setup firmware's UART0 TX pad,
  the PDS power domain, the pad OE: the BL616's UART registers were identical to the working test firmware; a pad
  sample loop read `I=0` on UART-function pads (meaningless). The BL616 was never the problem: the K230 shell answered
  a `link_stat` the setup firmware sent.
- **`pt_uart send` appends `\r\n`**: the splitter rejects a `\r` before `\n` (frame = `$<body>*XX\n`), so a frame sent
  that way is console text. `pt_uart frame <body>` added (CRC, `\n` only).
- **The poll() patch was not the cause.** install.sh's "poll re-arm" patch (2026-10-18, test012) re-checks every fd
  after the timeout, so even a lost wake-up would have shown the data at the deadline; with devfs's poll it never can.
- **The VM's USB had dropped the board** before the runs (`hub_ext_port_status failed (err = -32)`, no /dev/leakcam-*):
  unbind + bind 0000:0a:00.0 (xhci_hcd) brought both consoles back.
