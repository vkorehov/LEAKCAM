# test144: lighting changes against real changes (the leak comparison's false alarms)

Status: **step 1-2 built and run 2026-11-12 (host, real frames)**: imgdiff finds every real change (16/16) and keeps
every unchanged pair same, but calls 23 of 30 lighting changes a change. The target for what comes next (the
flash/no-flash check, then the change net on the KPU): REAL 100 %, LIGHT false alarms 0 on this set.

Why (user 2026-11-12): the job now shoots in the room's light when it is enough (energy: the LEDs only when it is too
dark). A room-lit frame compared with an earlier one sees daylight, lamps and shadows move: those must not be leaks,
and a puddle must never be taken for a shadow.

## The problem, thought through

- **LED-only shots (a dark room):** the light is fixed by construction (the LEDs sit on the board), so any difference
  is real: plain imgdiff is right. Kept as its own history (`cam<N>.hist`).
- **Room-light shots:** the light varies. Separate history (`cam<N>.amb.hist`), but it still changes with the time of
  day, the lamps and people. What a lighting change does: the level (gain, gamma: imgdiff already normalises the gain),
  a smooth gradient (a window, a lamp), a soft or hard-edged shadow; the texture under it stays. What a real change
  does: an object replaces the texture locally; water darkens the floor (texture kept, sharp edge: the same as a hard
  shadow) and shows the LEDs' and the lamps' reflections.
- **Measured here:** a per-block "texture kept" correlation (each block's mean and contrast out) separates the
  gradients and shadows (0.99-1.00) from objects - but not from wet floor (1.00, as a shadow) and not from changes on a
  flat ceiling (no textured block to judge). No single classical feature splits wet floor from a hard shadow.
- **The plan that sidesteps the lighting: flash / no-flash.** When a room-light check says `changed`: two frames at
  one exposure, one with the LEDs and one without; their difference is the scene lit by the LEDs alone - the same at
  noon and at night - compared with the LED history. Water shows strongly in it (the LEDs' specular reflections).
  ~3 LED frames, only on a room-light `changed` (rare): the false alarms of daylight, lamps and shadows go, the LEDs
  stay off on every ordinary check. Limit: in strong sun the LEDs' share is small, the difference noisy (measure).
- **Then the change net (KPU, DESIGN.md 5.3.12):** a `light` verdict on the pairs imgdiff calls changed, trained and
  judged on this same set plus real recordings (water poured on the bench floor by day and by night).

## Steps

1. `capture_frames.py`: the bench scene through the product's photo path, both cameras, `lit=led` and `lit=room`
   (the job's test token) -> `frames/<UTC>_<led|room>_cam<N>.jpg`. More sets over a day (morning, noon, evening, lamp
   on) make the set richer: rerun it at those times.
2. `lightsep.py [--save]`: pairs on the host with the product's imgdiff.c (built for this machine): SAME (itself,
   noise), LIGHT (gain, gamma, gradients, soft and hard shadow, the real LED <-> room pair), REAL (object, puddle with
   reflections, wet-floor darkening with a hard edge, a water streak); verdicts per class, the texture feature beside.
3. (next) flash / no-flash on the board: the LED-only difference image against the LED history, for each LIGHT and
   REAL class on real scenes (a lamp switched, a person's shadow, water on the floor).
4. (next) the change net's `light` verdict on the same pairs.

## Pass

Now: every REAL pair changed and every SAME pair same (no missed leak, no alarm on nothing). LIGHT is measured and
listed; it becomes a pass condition (0 false alarms) with step 3.

## Result

2026-11-12, 4 frames (bench scene: the ceiling, both cameras, LED-lit and room-lit, 08:36 UTC):

| Group | Class | Pairs | imgdiff changed | Texture kept (median corr of changed textured blocks) |
|---|---|---|---|---|
| SAME | itself, noise sd 3 | 8 | 0 | - |
| LIGHT | gain x0.6 / x1.4 | 8 | 1 | 0.84 |
| LIGHT | gamma 0.8 | 4 | 4 | 1.00 |
| LIGHT | gradient (side light), radial (lamp) | 8 | 8 | 0.99-1.00 |
| LIGHT | soft shadow, hard shadow | 8 | 8 | 0.68-1.00 |
| LIGHT | LED -> room light (the real pair) | 2 | 2 | 0.28-0.97 |
| REAL | object, puddle, wet floor, water streak | 16 | 16 | -0.15-1.00 (wet floor 1.00, flat-area changes 1.00) |

imgdiff: REAL 16/16, LIGHT false alarms 23/30. The texture feature alone would miss 11 of 16 real changes at the
threshold that clears the lighting: not usable alone (wet floor = shadow).

Seen on the frames: in the room's light both cameras' pictures are better than with the LEDs (cam1's LED frames
have a blue haze: the LED strips' light into its lens on the open bench board; check in the enclosure).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | n/a: a host test (step 1 uses the running board) | test022 | |
| This part's boot share | n/a: nothing at boot | - | |
| Test run time | step 1 ~20 s (two photos), step 2 ~5 s on the host (2026-11-12) | - | |
| Speed / rate against the top | n/a: a detection-quality test (the confusion matrix is its figure) | SPEEDS.txt | |
| Current idle / during | n/a: host | floor (test031) | |
| Voltage under load (min) | n/a: host | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: host | - | |
| Per day | n/a: host | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | n/a: host | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | n/a: host (the board's photos: test143) | no leak | |
| Image (sizes) | n/a: host (imgdiff.c built for this machine) | minimal image (rule) | |
| Compile flags | imgdiff.c -O2 -shared on the host | smallest code (rule) | |
| Features over the minimal image | the job's `lit=led` / `lit=room` test token (job.c) | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |
