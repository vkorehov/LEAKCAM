# test041 history

## Runs

None. Written 2026-10-30 (dab5fd2) for the first product's humidity slice (attic/firmware/bl616/main.c
HUM_ALARM_X10); dropped 2026-11-06 (5fc1138: "the AHT20 reads the board's heat"), removed 1b9f539; restored 2026-11-10
for climate.c (DESIGN.md 5.3.15: the room only after 3 h cool, the alarm at any reading).

## Obstacles

- 2026-11-06 (test048, bugs/v2.txt 12): on USB power the AHT20 read 42.6 C in a 21 C room (the K230 and the charger
  warm the board). Answer (user, 2026-11-10): read the room only after the board was cool for 3 h; a warm board only
  lowers the humidity it reads, so the 85 %RH alarm needs no window.
