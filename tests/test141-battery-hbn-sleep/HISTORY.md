# test141 history

## Runs

### Automated run 2026-11-14 (run.py, 43 s): **FAIL**

not run: the relay box did not unplug USB-C 1


### Automated run 2026-11-12 (run.py, 101 s): pass

7/7 checks pass.


### Automated run 2026-11-12 (run.py, 105 s): pass

7/7 checks pass.


### Automated run 2026-11-12 (run.py, 102 s): pass

7/7 checks pass.


### Automated run 2026-11-11 (run.py, 102 s): pass

7/7 checks pass.


### Automated run 2026-11-10 (run.py, 101 s): pass

7/7 checks pass.


### Automated run 2026-11-10 (run.py, 103 s): pass

7/7 checks pass.


### Automated run 2026-11-10 (run.py, 144 s): **FAIL**

6/7 checks pass. Failed: USB plug: the console back (ACOMP0 wakes it) (not in 30 s).


### Automated run 2026-11-10 (run.py, 103 s): **FAIL**

5/7 checks pass. Failed: asleep: HBN wakes counted (hbnw), boots unchanged (hbnw 1, boots 139 -> 142); USB plug: this boot a wake from HBN (hbn
k230:: command not found.
bouffalolab />
bouffalolab />clock
clock: 1794295319 (set 0 times)
bouffalolab />
bouffalolab />).


### Automated run 2026-11-10 (run.py, 102 s): pass

7/7 checks pass.


None yet.

## Obstacles

- **2026-11-11 05:58 UTC, crash at the start: adb to the phone gone (bench).** The phone had rejoined VK_5GHZ at its other
  address with a new port; VK_5GHZ removed from the phone (user), short adb calls reconnect and retry once. Rerun: pass.

None yet.

## 2026-11-10 09:22 UTC+2: FAIL 6/8, both test-side (fixed)

- "boots unchanged: 139 -> 142": the start count came from the server's last check-in, older than the runner's reset
  and test140's switches before this test (07:19:59 -> 07:20:22 UTC on USB). Through the sleep the count stayed 142 and
  every wake counted as hbnw. The start count is now taken from a check-in of this boot.
- "this boot a wake from HBN": the `hbn` answer was lost - the console came back from the plug mid-line ("k230::
  command not found"). Asked until it answers.
The sleep itself passed: asleep 2 s after the unplug, check-ins 22 s apart, room read at a wake, USB wake in 4 s,
clock -2 s.

## Obstacle 2026-11-10 23:55 UTC+2: the VM missed the board's wake (fixed in the test)

The sleep passed again (asleep 2 s after the unplug, check-ins 22-23 s apart, hbnw counted, boots unchanged, room read
at a wake). After the plug the console was not back in 30 s: the board had woken, but the VM's USB controller missed
its re-enumeration - it showed up right after the harness's xhci reset (23:55:13). The test waited only for the device
file; it now waits with board.bl_back(45), which includes that recovery.
