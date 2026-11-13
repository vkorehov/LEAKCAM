# test149 history

Runs and obstacles, newest first.

- 2026-11-13 20:30 UTC: pass 22/22, with the cameras' names (Bottom / Top camera).
- 2026-11-13 20:17 UTC: 21/22: "used again: new,new" read `dark,new`: the bench's bottom camera really was dark
  (a room without light, see below). The check now accepts a really dark camera.
- 2026-11-13 20:13 UTC, first run: 19/22, all three the test's own: it waited for /sdcard/leakcam/ignore to hold 0,
  a file that does not exist until a camera is first ignored (0 with no file now); and its baseline check (the real
  scene) opened a leak before the dark step (cleared before the steps now).
- Obstacle found by this test's bench (2026-11-13): the bottom camera read mean 15 in a dark room although the same
  scene gave 105: cap_grab_all kept the first frame at once when the stored exposure was already at its limit, a frame
  exposed before the LEDs' light (vicap_cap.c: at the limit it now measures once more CAP_APPLY frames on; 105-111 in
  5 checks after it, 120 ms more LED time per check at the limit).
