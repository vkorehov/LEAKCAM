# test036: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-10-31 (run.py, 346 s): pass

12 sessions: normal: rails off 27.73-27.74 s, first heartbeat 0.91-1.42 s; hang: rails off 7.32-7.32 s, first heartbeat 0.91-1.41 s; reboot: rails off 32.39-32.39 s, first heartbeat 1.41-1.41 s; noboot: rails off 15.31-15.31 s.


### Automated run 2026-10-25 (run.py, 345 s): pass

12 sessions: normal: rails off 27.86-27.86 s, first heartbeat 1.53-1.53 s; hang: rails off 6.84-6.85 s, first heartbeat 1.52-1.53 s; reboot: rails off 32.47-32.47 s, first heartbeat 1.53-1.53 s; noboot: rails off 15.31-15.31 s.

### 2026-10-22, 10 cycles each ([cycles_2026-10-22.txt](cycles_2026-10-22.txt))

Status: **pass** (2026-10-22): 39 supervised sessions (10 normal, 10 hang, 10 no-boot, 9 reboot) all end with the rails off, timings within a few ms; found: a K230 restart (or a hung agent with a live heartbeat thread) is invisible to the heartbeat and sessions had no length limit -> product: SESSION_MAX_MS 15 min backstop, SESSION_MAX_RESTARTS 2, BOOT_TIMEOUT_MS 90 -> 10 s.

| Case | Runs | First heartbeat | End |
|---|---|---|---|
| normal | 10 | 0.93-1.45 s | last edge 25.44-25.46 s, rails off **27.76-27.77 s** |
| hang at 5 s | 10 | 0.93-1.46 s | last edge 5.00 s, rails off **7.32 s** (every run) |
| no boot, 15 s timeout | 10 | none | END_NO_BOOT, rails off **15.32 s** |
| reboot pulse at 5 s | 9 | 0.93-1.45 s | gap **1.015 s** every run, never noticed; rails off 32.46-32.47 s |

(One no-boot run's lines landed in the next run's capture; counted as no-boot. The tenth reboot run was cut by the
script being stopped.) No session stayed on; the K230 console (CH340) came and went with the rails, not used.

**Found (product, fixed in attic/firmware/bl616/main.c):**
- **A K230 restart is invisible to the heartbeat** (back within ~1 s, under the 2 s limit) and so is **a hung agent
  whose heartbeat thread still runs** (the heartbeat is a thread of its own, leakcam_agent.c), and sessions had **no
  length limit**: a crash loop or such a hang keeps the K230 on until the battery is flat. The product does see a
  restart as a second READY (it answered it with WAKE). Now: `SESSION_MAX_MS` 15 min (a backstop above the agent's own 10 min hook limit) and `SESSION_MAX_RESTARTS` 2
  end a session as a failure (rails off, then the existing retries and back-off), logged as `session-end ... cut`.
- **BOOT_TIMEOUT_MS 90 s -> 10 s**: the heartbeat (READY with it) starts 0.9-1.4 s after power-on; a K230 that never
  boots now costs 10 s per attempt (3 attempts, then the 5 min / 1 h back-off).

Not covered here: the product's READY / link path itself (the test firmware has no link protocol) - with the product
build (rewrite); a real hang inside the agent (a hook that loops) - the session cap covers it whatever its form.

### 2026-10-22, one of each

| Case | Heartbeat | End |
|---|---|---|
| normal | first edge **1.44 s** after power-on, last at 25.4 s | no edge for 2.0 s -> rails off at 27.8 s |
| hang at 8 s (reset held) | last edge at 8.0 s | **rails off at 10.3 s** (2.0 s detection + 0.3 s discharge) |
| reboot at 8 s (50 ms reset pulse) | gap of only **1.0 s**, then a fresh agent (first edge 0.9-1.4 s after a start) | **not noticed**: the restarted agent ran a whole session, rails off at 35.5 s |
| noboot (BOOT1 high), 15 s timeout | none | END_NO_BOOT at 15.0 s, rails off 15.3 s |

Superseded text:

- Purpose paragraph: "no READY within the boot timeout (90 s, to tighten after test022) -> rails off, retry, back off
  after 3 failures."
- Setup: "The failure cases are made by the BL616 (when this ran the K230 console needed a VMware click per session;
  since test039 it is the BL616's `/dev/ttyACM1`, no click)"
- Boot time section: "The boot timeout should follow test022's measured boot (e.g. 3 x the median) instead of 90 s."
- Battery energy section: "Energy of a failed session (boot timeout x the session floor): what one hang costs, x the
  retries."

## Obstacles

(Since 2026-10-23 the K230 console is the BL616's `/dev/ttyACM1`, over the link UART from the first SPL line,
up across cold boots (tests/test039): the CH340 problems below are history, kept as the record of these runs.)

- **The K230 console is not available per session** (the CH340 lives on the switched 3V3 and needs a VMware click
  each time) -> the failure cases are made from the BL616 (reset held / pulsed, BOOT1 high).
- **A "heartbeat" edge 239-249 ms after power-on** - before any agent (app init is at 0.48 s): ALIVE is the K230's
  JTAG_TCK at reset and the boot toggles it -> edges in the first 400 ms are ignored; the product only watches the
  heartbeat after READY, so it never counted it.
