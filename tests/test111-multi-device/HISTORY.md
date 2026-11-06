# test111: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-04 (run.py, 324 s): **FAIL**

24/25 checks pass. Failed: app: the deleted board back in the list by itself (USB, server online) (75 s, ).


### Automated run 2026-11-04 (run.py, 203 s): **FAIL**

18/25 checks pass. Failed: setup Done (0.0 s); pairing: the app entered the PIN, no PIN prompt on the phone (dialog started 0x); known board: the short 'change its Wi-Fi' flow; board: last check-in reached the server (0x03) (no answer); app: the board online throughout (7C3E8262B149: offline, last seen 57 s ago, Wi-Fi "<network>"); app: the board still listed after the delete (7C3E8262B149: offline, last seen 1 min ago, Wi-Fi "<network>"); app: the deleted board back in the list by itself (USB, server online) (75 s, ).


### Automated run 2026-11-04 (run.py, 354 s): **FAIL**

20/25 checks pass. Failed: setup Done (0.0 s); pairing: the app entered the PIN, no PIN prompt on the phone (dialog started 0x); known board: the short 'change its Wi-Fi' flow; board: last check-in reached the server (0x03) ([0m[36mbouffalolab />[0m); app: the deleted board back in the list by itself (USB, server online) (75 s, ).


### Automated run 2026-11-01 (run.py, 187 s): pass

25/25 checks pass.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 135 s): pass

24/24 checks pass.

- phone USB port was the power source: swapped to sink


### Run with the status screen 2026-11-01 11:24 (run.py): pass 18/18

After four runs that failed on the test driver and one app bug (below), all checks pass.

### Automated run 2026-11-01 (run.py, 116 s): pass

18/18 checks pass.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 102 s): pass

18/18 checks pass.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 115 s): **FAIL**

17/18 checks pass. Failed: app: the board listed and online.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 144 s): **FAIL**

15/16 checks pass. Failed: setup Done (0.0 s).

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 128 s): pass

12/12 checks pass.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 09:52 (run.py, 137 s): pass

12/12 checks pass (first run).

- phone USB port was the power source: swapped to sink


## Obstacles

- 2026-11-01, status screen: the driver's scrolling find() lost the enabled "set up" button for 40 s while the list
  redrew (now UiAutomator's own wait for it); the phone moved the app to the background a second after pairing (now
  brought back to the front); a second Find after the status screen missed the board once (no longer tapped); a fixed
  6 s wait missed a slow first presence refresh (now polled). App bug found by it: the status screen opened the setup
  with the board listed but not picked, Send said "pick your board" (fixed: openSetup picks it).

- 2026-11-01: a console command typed right after the board started checking in came back "command not found" (the Wi-Fi
  stack's log lines interleaved with it); run.py retries the `presence` read once. The same log flood cut test106's
  product detection short (it read `help`): phone.product() now asks `prov` itself.
