# test135-app-screens: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-12 (run.py, 143 s): pass

35/35 checks pass.


### Automated run 2026-11-12 (run.py, 148 s): pass

35/35 checks pass.


### Automated run 2026-11-11 (run.py, 362 s): pass

35/35 checks pass.


### Automated run 2026-11-10 (run.py, 153 s): pass

35/35 checks pass.


### Automated run 2026-11-10 (run.py, 153 s): pass

35/35 checks pass.


### Automated run 2026-11-10 (run.py, 135 s): pass

35/35 checks pass.


### Automated run 2026-11-10 (run.py, 138 s): pass

35/35 checks pass.


### Automated run 2026-11-10 (run.py, 151 s): pass

35/35 checks pass.


### Automated run 2026-11-10 (run.py, 145 s): pass

35/35 checks pass.


### Automated run 2026-11-10 (run.py, 145 s): pass

35/35 checks pass.


### Automated run 2026-11-09 (run.py, 149 s): pass

35/35 checks pass.


### Automated run 2026-11-09 (run.py, 143 s): pass

35/35 checks pass.


### Automated run 2026-11-09 (run.py, 147 s): pass

35/35 checks pass.


### Automated run 2026-11-09 (run.py, 150 s): pass

35/35 checks pass.


### Automated run 2026-11-09 (run.py, 134 s): pass

35/35 checks pass.


### Automated run 2026-11-09 (run.py, 138 s): pass

35/35 checks pass.


### Automated run 2026-11-09 (run.py, 145 s): pass

35/35 checks pass.


### Automated run 2026-11-09 (run.py, 160 s): pass

35/35 checks pass.


### Automated run 2026-11-09 (run.py, 149 s): pass

35/35 checks pass.


### Automated run 2026-11-08 (run.py, 143 s): pass

35/35 checks pass.


### Automated run 2026-11-08 (run.py, 158 s): pass

35/35 checks pass.


### Automated run 2026-11-08 (run.py, 149 s): pass

35/35 checks pass.


### Automated run 2026-11-08 (run.py, 168 s): pass

35/35 checks pass.


### Automated run 2026-11-08 (run.py, 20 s): **FAIL**

not run: no Android phone on adb: Wireless debugging on, its <ip>:<port> in /home/vkorehovs/.leakcam_phone.


### Automated run 2026-11-07 (run.py, 141 s): pass

35/35 checks pass.


### Automated run 2026-11-07 (run.py, 190 s): **FAIL**

29/35 checks pass. Failed: 2: the status text says the next check-in; 4: the note names the next check-in; 5: the pending list in order: Wi-Fi, photo, video; 7: Not a leak clears it at the server (25.8 s); 7: the note says it watches again from its next check-in; 6: the camera inference -> Possible leak, inference.


## Obstacles

- no Android phone on adb: Wireless debugging on, its <ip>:<port> in /home/vkorehovs/.leakcam_phone (the port changes when it is switched off and on) (2026-11-08, automated run)

## 2026-11-07 22:26 UTC+2: pass 35/35

First run 22:22: 29/35. The screen dump writes a text holding double quotes in single quotes (`text='… Wi-Fi
"TESTNET" …'`): the parser read only double-quoted values, so the status text and the pending list read empty, and
the Not a leak tap (after a failed step) cascaded. Fixed in run.py (both quote styles, entities decoded); the app was
right. Obstacle: the phone was in the person's hands (WhatsApp) between runs: the test takes the screen for ~2.5 min.
