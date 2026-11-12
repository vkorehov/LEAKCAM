# test140 history

## Runs

### Automated run 2026-11-12 (run.py, 156 s): **FAIL**

14/16 checks pass. Failed: 84 %RH: no alarm (climate now
bouffalolab />
bouffalolab />climate: now: 42.2 C 84.0 %RH, cool 3 s); 90 %RH: the server's leak `humidity high` at once (leak {'since': '2026-11-12T05:14:30.186375634+02:00', 'why': 'camera change'}).


### Automated run 2026-11-12 (run.py, 202 s): **FAIL**

13/16 checks pass. Failed: 84 %RH: no alarm (climate now
bouffalolab />
bouffalolab />climate: now: 41.5 C 84.0 %RH, cool 3 s); 90 %RH: the server's leak `humidity high` at once (leak {'since': '2026-11-12T05:08:47.606784971+02:00', 'why': 'camera change'}); USB back: the K230 up, its start check done (not in 90 s).


### Automated run 2026-11-12 (run.py, 108 s): pass

16/16 checks pass.


### Automated run 2026-11-12 (run.py, 118 s): pass

16/16 checks pass.


### Automated run 2026-11-11 (run.py, 687 s): pass

16/16 checks pass.


### Automated run 2026-11-11 (run.py, 92 s): **FAIL**

15/16 checks pass. Failed: the BL616's free heap kept ((171028, 141444) -> (141636, 140896)).


### Automated run 2026-11-10 (run.py, 90 s): pass

16/16 checks pass.


### Automated run 2026-11-10 (run.py, 93 s): pass

16/16 checks pass.


### Automated run 2026-11-10 (run.py, 85 s): **FAIL**

14/16 checks pass. Failed: battery: the check-in carries troom/rhroom (that reading), roomago, quiet >= window (troom 288, rhroom 362, roomago 1, quiet 15 (reading (289, 362))); right after the K230 session: cool < window, no room reading (climate now
bouffalolab />
bouffalolab />climate: now: 28.6 C 36.2 %RH, cool 0 s).


### Automated run 2026-11-10 (run.py, 82 s): pass

16/16 checks pass.


### Automated run 2026-11-10 (run.py, 89 s): pass

16/16 checks pass.


### Automated run 2026-11-10 (run.py, 128 s): **FAIL**

15/16 checks pass. Failed: USB, 90 %RH: CHECK humid to the running K230 (climate now
bouffalolab />
bouffalolab />climate: now: 30.6 C 90.0 %RH, warm 0 s
climate: humidity 90.0 %RH >= 85: alarm).


### Automated run 2026-11-10 (run.py, 106 s): **FAIL**

12/13 checks pass. Failed: USB, 90 %RH: CHECK humid to the running K230 (climate now
bouffalolab />
bouffalolab />climate: now: 27.8 C 90.0 %RH, cool 0 s
climate: humidity 90.0 %RH >= 85: alarm).


None yet.

## Obstacles

- **2026-11-11 06:06 UTC, "free heap kept" failed 171028 -> 141636 B (test side).** The BL616's free heap swings ~30 KB
  between two levels with the K230's and the Wi-Fi's state (test109 the same); the low-water mark moved only 548 B. A
  single sample at each end is not comparable: both ends now take the best of samples across a check-in.
- **2026-11-11 05:57 UTC, crash: `adb shell am broadcast` timed out (bench).** The phone had rejoined VK_5GHZ at its
  other address with a new Wireless debugging port; VK_5GHZ removed from the phone (user), and short adb calls now
  reconnect through the relay app and retry once (phone.adb()).

- **2026-11-10 01:33 UTC: the first flash with climate.c stopped the board before USB.** metrics_boot() runs before
  board_init() (main.c: the boot count and the reset cause first); the new AHT20 mutex was made there with
  xSemaphoreCreateMutex(), i.e. from the heap, which board_init() sets up: NULL -> vAssertCalled() -> abort(), before
  the USB console and before the watchdog: no console, no `isp`, no reset (the battery keeps the BL616 powered through
  the relay box's unplug). Fix: xSemaphoreCreateMutexStatic() (configSUPPORT_STATIC_ALLOCATION 1 in both firmwares).
  Recovery needs BOOT + EN by hand. Rule (skill leakcam-product-build): nothing allocates before board_init().

## 2026-11-10 09:19 UTC+2: one check too strict (fixed in the test)

"the check-in carries troom/rhroom (that reading)": troom 288 vs the console's reading 289. The check-in reads the room
itself first thing (the board cool past the window), a second after `climate now`: a newer reading, one sensor step
apart. The check now allows 0.3 C / 1 %RH between the two.
The same check-in reading also added one to the room count read before step 3 ("right after the K230 session: ... no
room reading" failed on the count, the reading itself said cool 0 s): the count is now taken right before the session.
