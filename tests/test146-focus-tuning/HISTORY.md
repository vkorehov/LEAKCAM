# test146 history

## 2026-11-12: step 1 (host), FocusGuide

- First run 5/6: the noisy case (3 % noise, seed 3) oscillated TOO_FAR <-> WRONG_WAY forever: the best kept a
  noise-high median, so "within 3 % of the best" was never reached, and after a reversal past the peak a fall read as
  "wrong way" (no 8 % rise from the new start first). Fixed: after a reversal the best is the last pass's best; once a
  peak was passed, any fall means passed again (TOO_FAR).
- The first NO_DETAIL test scene was wrong (it had a real peak at the start); NO_DETAIL is now "no rise or fall for
  150 updates (~10 s)", tested on a flat, noisy scene.
- Then 7/7, including the sweep (hand 1 / 2 / 4 degrees per score, noise 0 / 3 / 5 %, peak on either side).
