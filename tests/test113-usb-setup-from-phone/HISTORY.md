# test113: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-07 07:48 (run.py): pass

18/18 checks pass (step 12 with the driver's 12 s hold after Done).

### Automated run 2026-11-07 07:44 (run.py, 201 s): **FAIL**

17/18 checks pass. Failed: change Wi-Fi: Done stays when the board's relay comes back (). The test's fault, not the
app's (Obstacles: the driver's end ends the app).


### Automated run 2026-11-06 (run.py, 141 s): pass

13/13 checks pass.


### Automated run 2026-11-06 (run.py, 148 s): pass

13/13 checks pass.


### Automated run 2026-11-06 (run.py, 157 s): **FAIL**

12/13 checks pass. Failed: the cable pulled during the check: the app does not claim Done (Enter the Wi-Fi network 7C3E8262B149 should use (2.4 GHz; leave the pa).


### Automated run 2026-11-06 (run.py, 65 s): pass

8/8 checks pass.


### Automated run 2026-11-06 (run.py, 78 s): **FAIL**

7/8 checks pass. Failed: the app used the cable (no Bluetooth pairing) (3 screens).


### Automated run 2026-11-06 (run.py, 136 s): **FAIL**

not run: the phone was never unplugged


### Automated run 2026-11-06 (run.py, 153 s): **FAIL**

5/8 checks pass. Failed: the plug opens the app on the cable screen of this board, no network (not within 30 s); the app's setup ran on the cable screen (no list, no Find) (); the app used the cable (no Bluetooth pairing) (4 screens).


### Automated run 2026-11-06 (run.py, 178 s): **FAIL**

5/8 checks pass. Failed: the plug opens the app on the cable screen of this board, no network (not within 30 s); the app's setup ran on the cable screen (no list, no Find) (); the app used the cable (no Bluetooth pairing) (5 screens).


## Obstacles

- **2026-11-07, `provision erase` not run (3 times today):** the first command on the BL616 console right after the run
  started got no answer and the board kept its network (step 1 FAIL `full`); by hand the same erase + reboot worked
  every time. phone.erase() now checks the answer and retries (logged: `provision erase not answered (try 1 of 3)`,
  the 2nd try worked each time). Cause open: the first write on a freshly opened CDC port of the BL616 after its
  re-enumeration (board.bl() already sends an empty line first after a reconnect of 10 s or more; this one was
  shorter).
- **2026-11-07, the driver's end ends the app:** step 12's "Done stays" read the app's log after BleOp `app` returned;
  `am instrument` ends the app's process with the op, the board's relay came back after that and the plug launched a
  new app process on the cable screen: FAIL without an app fault. BleOp `app` got `hold` (seconds the screen is still
  watched after the send); step 12 holds 12 s, the relay comes back 4.6 s after Done inside it.
- **2026-11-07, hand mix-up, the wrong cable twice:** the phone went onto the BL616's console cable (USB-C 2, the
  multi-CDC "LEAKCAM BL616 console") instead of USB-C 1: the app found no LEAKCAM on the cable and set the board up
  over Bluetooth, the runner lost the console. Seen from the PC: the right move leaves /dev/leakcam-bl616 and
  /dev/leakcam-k230 and removes /dev/leakcam-k230usb. run.py now checks that 1 s after the plug and stops with an
  ACTION line naming the right port.

- **2026-11-06, the VM's USB controller stopped again at firmware switches** (3 times today, `hub_ext_port_status
  failed`, err -32): the BL616 re-enumerates at every switch (a reboot into the other slot) and VMware's passthrough
  controller sometimes stops for every device. board.bl() now resets it (xhci unbind/bind) after 10 s without the
  port, only when the kernel log shows that fault (a sleeping BL616 has no port either); worked in the 09:12 run.
- **Step 8 first judged from the driver's screen samples**: the app shows the "unplugged before it confirmed" error
  only a moment and goes to the board list; the samples missed it (FAIL although the app was right). Judged from the
  app's log now; the op limit 15 s (the list has no Send button to wait for).

- **2026-11-06, the plug's wake never powered the K230:** the bench cable pulled -> the setup firmware sleeps (PDS);
  the phone's plug wakes it, the session starts, `crash: K230 failure recorded: 3V3 did not come up`: K230_PWR (IO30)
  drove nothing after the sleep (the low-power layer's pad setup). The phone saw no device (`num_connects` unchanged),
  the app fell back to BLE. A boot with the phone already in worked, so only the wake path failed. Fix:
  `k230_power_on()` sets every K230 pin again first (bl616_common/k230_power.c); proven by the next replug (rails in
  1.6 ms, READY 0.82 s) and the passing run.
- **`k230 usbboot` refused while a setup session ran** (no network stored = the K230 on for the cable): `k230 off`
  cut the session and it recorded "heartbeat stopped" as a K230 failure (one false record, build 202611052104). Fix:
  the session ends without a record when the rails were switched off (`k230 off`); k230_usb_boot.py ends a running
  session before `usbboot` (ROM on USB 5.8 s with a session running, no record).
- **The test driver scrolled the screen up and down for 2 min** after Done: BleOp's `busy()` also waited for the Find
  button, which the cable screen does not have; every poll scrolled to look for it until the 150 s timeout. Fixed
  (busy = Send only on the cable screen); timeouts cut to 2-3x the measured times (setup op 30 s, server 30 s, plug
  120 s; user: "i dont have that much time").
- **The check "used the cable" looked for "over the USB cable"**: that text (Sending ...) shows for milliseconds (the
  board answers in 1-3 ms) and was never seen; now the cable screen's own text ("Check and save") and no pairing.
- **Hand step mixed up:** "plug the phone in" by a hand file; the cable went to the PC at 08:46 (the K230 enumerated
  there) and the run fell back to BLE. The runner now sees the plug on the phone itself (`dumpsys usb`
  host_connected), no hand file.
- **The VM's USB controller dropped** (`hub_ext_port_status failed`, err -32) once more: xhci unbind/bind of
  0000:0a:00.0; the first console command after it (the erase) was lost, that run was void.
- A console logger opened `/dev/leakcam-bl616` with the tty's default echo: the board's own output went back into its
  shell (`k230:: command not found`); open the port raw (board.openp) for any second reader.
- The Android "always" choice was stored for the product name CanMV; after the rename to LeakCam the dialog came once
  more (answered, "always" ticked again).

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
