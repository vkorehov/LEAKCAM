# test047: the one K230 program on every power-up: the relay always, the leak job beside it (low level)

Status: **partly seen** (2026-11-07, in other tests' runs; no run.py of its own yet): step 1 in both firmwares (full:
WAKE `usb`, the relay, the job's first check 0.64 s after the K230's start: test049, test050), step 2 (STATUS every
60 s, no mode field: test048), step 3 (the setup firmware's relay ends on SAVED, the full firmware's relay back:
test113 step 12 bench run), step 6 (a session ended by `k230 off` on USB started again ~36 s later: test050 step 9).
Step 1 in the setup firmware too (test048's relay log, 2026-11-07 09:13 UTC: WAKE `setup` at 616 ms, the job's check
621-681 ms, the same version; the K230's clock set later from the setup firmware's own server answer, its WAKE had
none). Open: step 4 (a pull, by hand), step 5's battery reasons (test132).

DESIGN.md 5.3.8 (user, 2026-11-06): the K230 boots the same product program every time and knows nothing of the
BL616's firmwares or why it was powered: READY, the WAKE's data (the reasons only logged), then the **USB relay** (a
service that always runs) and the **leak job** (job.c, cameras simulated) in parallel. DESIGN.md 5.3.11: on USB power
both BL616 firmwares keep it on the whole time; on battery the full firmware powers it per leak check (5.3.12).

## Setup

Product K230 image and BL616 product pair; the BL616 console and the K230 console on the bench; USB-C 1 on the bench
(the PC as the phone, `/dev/leakcam-k230usb`).

## Steps

1. **The same start under every firmware:** setup firmware (no network, USB in) and full firmware (USB in): READY, the
   WAKE (`setup` / `usb`), the relay's CDC device on USB-C 1, the job's first check (`job: check` ... `DONE`) beside
   it; the same lines and the same version in both.
2. **STATUS every 60 s:** `STATUS,<uptime s>,<version>,<die x10>,<heap free>,<heap min free>,<pages free B>` with a
   fresh `;b=`, 60 +- 2 s apart (DESIGN.md 5.3.9).
3. **The setup ends on SAVED:** the setup firmware's relay ends with the K230 off 2-3 s after SAVED, the switch to the
   full firmware, whose session (WAKE `usb`) brings the relay back (`ID ... full`).
4. **A pull:** USB-C 1 pulled during the relay: the battery read by the running K230 at once, then `k230: off (USB
   unplugged)`, no crash record (a leak check running: off after its DONE instead).
5. **Every reason runs the same:** WAKE `rtc` and `leak` (test132, on battery) give the same relay + job start as
   `usb` and `setup`.
6. **A failed session on USB:** `k230 off` (or a K230 that stops its heartbeat) on USB power: the full firmware
   starts it again 30 s after the end.
7. **The sessions in the check-in (DESIGN.md 5.3.10):** each ended session counted (`ks`, `klast` 1 for an end
   asked for), `kver` = the K230 log's version.

## Pass

One program, the same start every time, the relay and the job together; the K230 on with USB power in both firmwares
and off only by the BL616.

## Needs (build features and dependencies)

K230: the product program (k230_app/leakcam.c, job.c). BL616: k230_session.c (both firmwares), usb_full.c,
usb_setup.c, leak_check.c. Tools: the bench as the phone (tools/test-runner/cable.py).
