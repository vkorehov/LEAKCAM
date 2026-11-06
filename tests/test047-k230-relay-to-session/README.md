# test047: the K230 from the USB relay to the normal product job (low level: the K230 program's job per power-up)

Status: **partly run** (2026-11-06, test048's relay, the bench as the phone: test048-checkin-metrics/relay_*.log):
step 1 pass (`WAKE setup taken` 795 ms after the run, STATUS setup,0, the CDC device 4.6 s after the erase), step 2
pass (STATUS at 60, 120, 180 s), step 3 pass (`k230: off (network saved)`, the CDC device gone 2.4 s after SAVED,
then the full firmware), step 6 pass (no relay device in any full-firmware run on USB power), step 7 pass (ks 1,
klast 1, kms 198116, kver = the log's, sw 1; the next ks 0, sw 0). Step 4 (a pull) needs a hand; step 5 needs the
full firmware's K230 session, not built.

One K230 image; the BL616's WAKE answer picks the job at each power-up (DESIGN.md 5.3.8): `setup` = the USB relay,
any other reason = the normal product job. There is no switch inside a running program: the relay ends with the K230's
power, the setup firmware switches to the full firmware, and the next power-up gets another reason. The periodic
`STATUS,<mode>,<uptime s>` (every 60 s and at the job's start, k230_link.h) names the job the K230 runs.

## Setup

Product K230 image and BL616 product pair; the BL616 console on PR1; USB-C 1 on the bench PC (the PC as the phone,
`/dev/leakcam-k230usb`) or on the phone (test113).

## Steps

1. **Relay starts:** no network stored, USB-C 1 in: `k230: WAKE setup taken`, the K230's first `STATUS setup,<s>`
   at the relay's start, the CDC device on USB-C 1 (1209:abd1, "LeakCam").
2. **STATUS every 60 s:** a relay held open 3 min: three more `STATUS setup` lines, 60 +- 2 s apart, each with a fresh
   battery (`battery` on the console; on USB power the reading is not kept, test046).
3. **Relay ends on SAVED:** a setup over the cable (test113's flow or the bench script): `k230: off (network saved)`
   2-3 s after SAVED, the CDC device gone from USB within 1 s of it, then the switch to the full firmware.
4. **Relay ends on a pull:** USB-C 1 pulled during the relay: `k230: off (USB unplugged)` at once, no crash record.
5. **The normal job after it:** the full firmware's next K230 session (cold or rtc): WAKE with that reason,
   `STATUS session,<s>` from the same image, no CDC relay device on USB-C 1. Needs the full firmware's K230 session.
6. **No relay with a network stored:** the phone or PC plugged into USB-C 1 on the full firmware: no K230 relay session
   starts (the full firmware never sends `setup`), no LeakCam CDC device appears.
7. **The job in the check-in (DESIGN.md 5.3.10):** after the relay ended on SAVED, the full firmware's first check-in
   carries `ks=1`, `klast=1` (ended as asked), `kms` ~ the relay's rails-on time, `kver` = the K230 log's `version`
   line, `sw=1` (the switch to the full firmware); the one after it `ks=0`, `sw=0`. A relay ended by a pull: `klast=1`
   too (an end asked for), no crash record.

## Pass

The relay runs only under WAKE `setup`, ends cleanly with the K230's power (saved or pulled, no crash record); the
next power-up runs the normal job from the same image; STATUS names the job every 60 s with a fresh battery; a board
with a network never offers the relay.

## Needs (build features and dependencies)

K230: the product program's STATUS (k230_app/leakcam.c). BL616: STATUS taken in k230_session.c (logged), the full
firmware's K230 session for step 5 (not built). Tools: the bench script answering the relay (test113's bench run).
