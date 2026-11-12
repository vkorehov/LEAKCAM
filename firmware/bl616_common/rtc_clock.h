/*
 * The HBN RTC as the time base (DESIGN.md 5.3.12, 5.3.15): a 40-bit counter at 32768 Hz in the always-on domain. It
 * runs through HBN sleep, every wake and every reset, and stops only with the battery: the wall clock, the metrics'
 * ages, the sleep schedule and the climate's cool window are kept against it (the core timer restarts at every wake).
 */
#ifndef LEAKCAM_RTC_CLOCK_H
#define LEAKCAM_RTC_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#define RTC_HZ 32768u

uint64_t rtc_clock_ticks(void);
/* seconds on the counter */
uint32_t rtc_clock_s(void);
/* the Y3 crystal clocks the RTC (before the first HBN sleep): it must already run when selected - selected while still
 * starting (~1 s after power-on) it stopped the counter and the chip never woke (2026-10-22); the counter watched up to
 * 3 s, else RC32K (the chip still wakes, less exact). Once selected it stays through sleeps and resets: then at once */
bool rtc_clock_xtal(void);

#endif
