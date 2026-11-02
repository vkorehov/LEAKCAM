# test124: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

- 2026-11-02 17:48 UTC: the watchdog fix proven (README Result).
- 2026-11-02 17:37-17:41 UTC: first fix (the clock ungated only): every sleep window ended in a watchdog reset a few
  seconds in (reset record 0x7d with `wdt`; tick count from 0 after the window). The `crash hang` that followed ran on a
  fresh start and passed, which first read like a pass.
- 2026-11-02 17:23 UTC: re-setup only (bflb_wdg_init again after the wake, clock still gated): the counter still stood
  still (3 -> 3), the hang was not reset: EN.
- 2026-11-02 17:11-17:14 UTC: `crash hang` on a fresh start: reset in 6.6 s with the record; after a window that never
  slept (USB power, no `usb`): the same; after a window with 17 sleeps (`wakeup_timer 30000 usb`): no reset: EN.
- 2026-11-02 morning: steps 1-2 (BOR_CFG, reset records 0x00 / 0x08).

## Obstacles

- 2026-11-02 07:26 UTC, `crash hang` in the setup test bed after a sleep window: the board stayed hung until the user's
  EN tap at 17:10 UTC (it looked unplugged: the console enumerated but took no data). The cause is the watchdog bug in
  README Result; ISP could not help (`isp` is a console command the hung firmware never runs).
- 2026-11-02 17:45 UTC, the VM's USB controller hung (`hub_ext_port_status failed (err = -32)` without end) after the
  board's reset into the test bed; rebinding the xHCI (0000:0a:00.0) brought it back (memory vmware-usb-connect-prompt).
- My probe scripts printed nothing: `os._exit(0)` skips Python's stdout flush, so a working console looked dead; the
  probes run with `python3 -u` since.
- `run_tests.py --help` started a real batch (it has no help); it only waited for the unplugged board and ended.
