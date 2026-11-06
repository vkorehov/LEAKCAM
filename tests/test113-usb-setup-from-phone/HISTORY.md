# test113: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

## Obstacles

- **2026-11-05, first run (interrupted):** the phone saw the board's USB device attach and drop 0.3 s later, every
  ~8 s, while Android's "open LEAKCAM Setup for this USB device" dialog was unanswered (the runner looked for the
  app's package in the dialog, which belongs to systemui: never answered). The app found no cable and fell back to
  BLE: Done, but over Bluetooth (the check caught it). Once OK was pressed ("always" ticked) the link stayed up. Why
  the board dropped the device every 8 s is not yet seen on the BL616 log: the next run watches it.
- The runner's check read `vendor_id=` from `dumpsys usb`; its history lists `manufacturer=`/`product=` and every
  attach, also ones that dropped at once: replaced by the app's own log line (`usb screen <id>`).
- The runner was started as `cd X && nohup python3 ... &`: `$!` was the shell, its kill left the runner running
  (it woke the phone for 8 more minutes). Start runners alone with the absolute path (memory background-runner-pid).
- The phone powers the board over the cable: its battery fell to 6 % during the session; charge it before runs.
