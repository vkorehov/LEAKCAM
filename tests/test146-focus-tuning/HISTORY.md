# test146 history

## 2026-11-12: step 1 (host), FocusGuide

- First run 5/6: the noisy case (3 % noise, seed 3) oscillated TOO_FAR <-> WRONG_WAY forever: the best kept a
  noise-high median, so "within 3 % of the best" was never reached, and after a reversal past the peak a fall read as
  "wrong way" (no 8 % rise from the new start first). Fixed: after a reversal the best is the last pass's best; once a
  peak was passed, any fall means passed again (TOO_FAR).
- The first NO_DETAIL test scene was wrong (it had a real peak at the start); NO_DETAIL is now "no rise or fall for
  150 updates (~10 s)", tested on a flat, noisy scene.
- Then 7/7, including the sweep (hand 1 / 2 / 4 degrees per score, noise 0 / 3 / 5 %, peak on either side).

## Obstacles

### 2026-11-13: live video, then the session moved from the cable to the server

- The cable version first (user: "focus screen should live stream video", "toggle between two cameras"): the K230's
  own USB device (test043: 3.3 MB/s) carried the shown camera's H.265 as FVID frames. Then (user: "focus tuning is done
  on already setup phone ... It should offer it on status screen"): through the server, no cable; the cable code
  (FVID, usb_frame, the relay's FOCUS lines and PIN snooping) removed.
- My test's IDR check looked for the VPS: the K230's encoder sends the parameter sets once, with the first frame,
  so later IDRs (and the requested one) carried none; the check now reads the NAL type (19/20), the decode puts the
  session's parameter sets first, as the phone's one decoder keeps them.
- `kd_mpi_venc_request_idr` was ignored: the first picture after both switches a P frame, the IDRs at frames 90 and
  180 (the GOP). The SDK samples call `kd_mpi_venc_enable_idr(chn, K_TRUE)` after the create: added, not yet measured
  (the bench away from the home LAN).
- The phone's focus screen title read "Focus · -1": inside `TextView(...).apply {}` `id` was the view's id (-1), not
  the activity's board ID; the field renamed `board`. The title now "LEAKCAM Focus Adjustment" (user).
- The app could not reach the server on the PC at all: Android's cleartext rule still named the Pi (.159); now the
  PC (.131) and 127.0.0.1 (a test build through adb reverse).
- The relay's late viewer waited forever on the test clip: libx265 makes CRA keyframes (NAL 21), the relay looked
  for IDR (19/20) only; now any random-access picture (16-21); parameter-set-only records always pass.
- app_mock.py's first screenshots were timed from the script's start: the app comes up ~5 s later, the "camera 1"
  shot showed the waiting screen and still passed (the spinner gave the area spread): shots now follow the mock
  board's "showing camera" events, the waiting state checked on its own.
- A focus session would have been cut by the BL616's 60 s check-in after 130 s (presence.c waited at most 130 s for
  a capture): it now waits for capture_busy() alone, bounded by capture.c's guard (secs + 60 s for a focus session).
