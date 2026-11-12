/*
 * The full firmware on battery sleeps in HBN between its events (DESIGN.md 5.3.12, user 2026-11-07; tests/test141):
 * once nothing runs (no K230, no leak check, no check-in) and the next event is 10 s or more away, HBN level 0 (36 uA,
 * tests/test031) until the earliest of the check-in, the leak check and the humidity reading (RTC), a probe edge
 * (ACOMP1) or the USB plug (ACOMP0). Every wake is a reset: what must go on (the dues, the probe state, missed
 * check-ins) is kept here in HBN RAM against the RTC (rtc_clock.h). On USB power it never sleeps (5.3.11).
 */
#ifndef LEAKCAM_HBN_SLEEP_H
#define LEAKCAM_HBN_SLEEP_H

#include <stdbool.h>
#include <stdint.h>

#define HBN_WAKE_LEAK 1u
#define HBN_WAKE_RTC  2u
#define HBN_WAKE_USB  4u

/* main(), right after metrics_boot(): a wake from HBN (its sources, HBN_WAKE_*; 0 = a reset or power-on: the state
 * starts again) */
unsigned hbn_sleep_boot(void);
/* this boot's wake sources, 0 = not a wake from HBN */
unsigned hbn_sleep_woke(void);
/* the sleep task, once at boot after the other tasks */
void hbn_sleep_start(void);

/* the dues and the state the other modules keep through the sleep (RTC s) */
uint32_t hbn_sleep_checkin_due(void);
void hbn_sleep_checkin_set(uint32_t rtc_s);
uint32_t hbn_sleep_leak_due(void);
void hbn_sleep_leak_set(uint32_t rtc_s);
bool hbn_sleep_wet(void);
void hbn_sleep_wet_set(bool wet);
unsigned hbn_sleep_missed(void);
void hbn_sleep_missed_set(unsigned n);

#endif
