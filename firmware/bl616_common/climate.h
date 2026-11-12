/*
 * Temperature and humidity, both firmwares (DESIGN.md 5.3.15, user 2026-11-10; tests/test140, test041): the AHT20
 * every 10 min. The room's values only from a board cool for 3 h or more (no K230, no USB power: the board's heat
 * sources, test048), read first thing; 85 %RH (the server's, thresholds.h) or more at any reading is a humidity alarm (a warm board only reads
 * lower): the firmware's own action (full: a check-in at once + a K230 leak check `humid`; setup: back to the full
 * firmware when a network is stored). No polling between readings (the setup firmware sleeps in PDS): the window is
 * restarted by the heat events (the K230's power-off from k230_power.c, the USB unplug by climate_heat()).
 */
#ifndef LEAKCAM_CLIMATE_H
#define LEAKCAM_CLIMATE_H

#include <stdbool.h>
#include <stdint.h>

/* once at boot, after what alarm() uses; warm(): a heat source on now (K230 rails, USB power). After a reset the window
 * starts again and the first reading is at once; after an HBN wake (hbn_wake) both go on from HBN RAM */
void climate_start(bool (*warm)(void), void (*alarm)(int rh_x10), bool hbn_wake);
/* the next reading due, RTC s (the full firmware's sleep wakes for it) */
uint32_t climate_due_s(void);
/* a reading now, in the caller's task (the full firmware's check-in: first, before the radio): the room after the
 * window, the alarm at the humidity threshold (thresholds.h) */
void climate_read(const char *why);
/* a heat source just ended (the USB unplug): the cool window starts now */
void climate_heat(void);

#endif
