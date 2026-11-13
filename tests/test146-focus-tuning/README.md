# test146: camera focus - bad focus detected, tuned by hand with the app's turn guide

Status: **the focus adjustment through the server built and passing without the board (2026-11-13)**: the app's
"LEAKCAM Focus Adjustment" against a local server with the board mocked from its recorded frames: 15/15 (app_mock.py:
live picture per camera, switches at the board in 5-56 ms, Stop on leaving, no decoder error). Since then (user
2026-11-13): the lens picture with a green (turn) and red (not) arrow, the first way random (unit tests pass; app_mock.py checks
the arrows in each shot, **not yet run**). An automatic switch to a hidden camera whose lens is turned was built and
removed the same day (it jumped between the cameras with hands at the board). Server: `go test`
(TestFocusCheckin, TestFocusRelay, TestFocusCmd) pass. Board side (K230 focus_run, BL616 `focus` capture) built, not
run on the board yet: the bench was away from the home LAN. Open on the board: the encoder ignored the IDR request at
a switch (2026-11-13 09:22, frames until the GOP's IDR); `kd_mpi_venc_enable_idr` at the channel's create built, not
yet measured.

Why (user 2026-11-12): "we need to determine bad focus ... the KPU can have a separate output saying it cannot
reliably calculate differences because no focus, in that case ask the user to start the focus tuning procedure",
"help user with direction where to rotate lens; if focus gets worse swap direction", "good UX for user to adjust
focus asap". A camera out of focus makes the leak comparison blind: a 10 cm puddle at 3 m is ~17x10 pixels when
sharp (tests/test144), a soft smear when not.

## How it works (DESIGN.md 5.3.17)

- The K230 scores sharpness per camera (Tenengrad over 5 areas, brightness-normalised) on every LED-lit leak check;
  below 60 % of the camera's reference on 3 checks in a row: `unfocused` in `job=` (no reference yet: `untuned`); the
  change net's `unreliable` output will say the same for fog, smudges and a covered lens.
- The server keeps the flag, notifies once and gives the board 60 s check-ins while it is flagged; the app's status
  screen offers **Tune focus** for that board (and ⋮ always).
- **LEAKCAM Focus Adjustment** (user 2026-11-13: no cable, "focus tuning is done on already setup phone"): Tune focus
  asks the server (`POST /v1/devices/<id>/focus`), the board's next check-in brings a `focus` capture, the K230 streams
  the shown camera's native H.265 and per-frame score to the server (`/v1/focus/<id>/stream`), the server relays it
  live to the app (`/v1/focus/<id>/live`); the camera buttons, Done and Stop go back through `/v1/focus/<id>/cmd`
  (the K230's long poll). The guide says which way to turn (↺ / ↻), when it went too far, when to stop; Done saves the
  reference on the board.

## Setup

- Step 1: the host only (`cd android && ./gradlew testDebugUnitTest`, aapt2 override on this arm64 host:
  android/README.md).
- Step 3a (no board): the phone on this host's USB (adb), the app and its test APK built for a local server
  (`-PleakcamServer=http://127.0.0.1:8090`); `app_mock.py` builds and starts the server here, `adb reverse` gives the
  phone its port over USB (no Wi-Fi), `tools/test-runner/mock_board_focus.py` plays the board from `mock_cam0.fv` /
  `mock_cam1.fv` (the board's recording of 2026-11-13: [u32 length][u8 IDR][access unit] per frame).
- Steps 2-6 (the board): the full firmware, the server on the PC with focus.go, the app's normal build on the phone, a
  textured target at ~1.5 m in front of each camera, the lens reachable (cover off). No motor: the lens is turned by
  hand when the step says so, or defocused by a known amount (90 degrees from its tuned point, a pen mark).

## Steps

1. **The guide (host, `FocusGuideTest`):** a simulated lens (sharpness peak at a position, the person turning the
   way the guide says, 1-4 degrees per score, noise 0-5 %): it ends on STOP within 15 degrees of the peak from either
   side; the other side starts with WRONG_WAY; starting at the peak still ends there; a flat scene (only noise) says
   NO_DETAIL; a jump says "slower".
2. **The session on the board (`run.py`, the bench as the app through the server):** Tune focus asked, the board's
   check-in brings it (under 60 s while flagged); the live records: FOC lines and pictures at 15 fps, every picture
   decodes (2592x1936, the session's parameter sets first); each camera switch's first picture an IDR within 300 ms
   of the command; Stop ends the board's upload, SENT, the bridge off; 3 check-ins after; the K230's page pool and the
   BL616's lwIP back (no leaked TCP: skill leakcam-test-metrics 5b).
3. **The screen:** a) without the board, `app_mock.py` (the mock board, a local server): the waiting state, the live
   picture per camera with the lens picture's green and red arrows, each tap's switch at the board, Stop when the screen is left, no decoder error; b) with the
   board and a hand: from 90 degrees off, the person follows the screen: the first hint's direction, a wrong way told
   within 1 s, STOP within 5 % of the score at the marked tuned point, Done -> `FOCUS,OK`; under 60 s per camera.
4. **Bad focus in a leak check:** the reference saved, the lens turned 90 degrees off: the 3rd LED-lit check reports
   `job=unfocused,...,<pct>,...` with the % below 60; the server sends one notification and 60 s check-ins; the
   status screen shows Tune focus; tuned back: `foc` above 90, the offer gone, new histories (`new`), no leak raised.
5. **Room light does not judge focus:** a room-lit check with the lens off: no `unfocused` from it.
6. **The change net's `unreliable`** (when the net has the output, firmware/NN.txt): defocused, fogged (breath on the
   lens), covered: `unreliable` on each, `changed` / `same` on a sharp lens as before.

## Pass

Step 1: every unit test. Steps 2-6: every check above, the phone left clean (memory phone-clean-after-tests), the
reference and the histories restored at the end.

## Result

- 2026-11-12, step 1: `FocusGuideTest` 7/7 (HISTORY.md: two fixes first).
- 2026-11-13, step 3a: `app_mock.py` 15/15: the waiting state (spinner, the start time), then camera 1 live, camera 2
  after the tap, camera 1 again ([focus_adjustment_mock_2026-11-13.png](focus_adjustment_mock_2026-11-13.png)); the
  board shows each tapped camera 5-56 ms after its command; Stop at the board when the screen is left; the phone's
  hardware H.265 decoder (OMX.Exynos.hevc.dec, up to 8192x8192) on 2592x1936 without an error.
- 2026-11-13, the cable version on the board (superseded the same day by the server version): live H.265 15.0 fps,
  ~10 Mbit/s, a score per frame; switches 48-93 ms but the first picture after a switch was a P frame: the encoder
  ignored `kd_mpi_venc_request_idr` (IDRs only with the GOP, frames 90 and 180). Fix built (`enable_idr` at create).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | **MISSING** (step 2) | test022 | |
| This part's boot share | n/a: the focus session is not at boot | - | |
| Test run time | step 1: 12 s (gradle, host) | - | |
| Speed / rate against the top | cam0: 168 frames (1 IDR), 15.0 fps, 9577 kbit/s, decoded 2592,1936,168, 168 scores; cam1: 90 frames (0 IDR), 15.0 fps, 10543 kbit/s, decoded 0,0,N/A, 89 scores; switches 68 ms, 48 ms | 15 fps (the sensor) | |
| Current idle / during | **MISSING** (step 2: LEDs on for the session) | floor (test031) | |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | **MISSING**: energy per tuning (1 min) | - | |
| Per day | n/a: a rare action | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | **MISSING** (step 2) | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** (step 2: 2 min of streaming) | no leak | |
| Image (sizes) | BL616 test firmware 1426720 B (2026-11-13, on the board at the run) | minimal image (rule) | |
| Compile flags | n/a until the K230 part is built | smallest code (rule) | |
| Features over the minimal image | the focus session (k230_app), FocusActivity + FocusGuide (app) | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |
