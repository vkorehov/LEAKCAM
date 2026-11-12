/*
 * The board's metrics in every check-in (DESIGN.md 5.3.10, tests/test136, user 2026-11-06: "all metrics possible"):
 * the sensors (AHT20 over I2C, the leak probe node, the BL616's die and supply), the battery (battery.h), uptime,
 * boots and the reset cause, crashes, the heap, this check-in's Wi-Fi (wifi_check.h), counters since the last good
 * check-in, and the K230 as its last STATUS frame and its last session left it. Kept in HBN RAM where they must outlive
 * a reset or the switch between the two firmwares; the sensors are read fresh before each check-in's Wi-Fi run (quiet
 * rails: the AHT20's RC filter is there against the Wi-Fi bursts).
 */
#ifndef LEAKCAM_METRICS_H
#define LEAKCAM_METRICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* each firmware's main(), once, early: the boot counted, the reset cause read and cleared */
void metrics_boot(void);

/* events counted until the next good check-in */
void metrics_switch(void);            /* a switch to the other firmware (switch_to_setup.c, switch_to_full.c) */
void metrics_usb_wake(void);          /* the USB plug woke the setup firmware from PDS (setup_sleep.c) */
void metrics_hbn_wake(void);          /* this boot is a wake from HBN sleep (hbn_sleep.c): counted as hbnw, not a boot */

/* the K230: its STATUS frame's arguments (k230_link.h), and a session's end */
void metrics_k230_status(const char *args);
void metrics_k230_session(uint32_t on_ms, bool ok);
/* a leak check's result, DONE's "<cam0>,<cam1>" (full firmware, leak_check.c): in every check-in from then on as
 * job=<cam0>,<cam1>&jago=<s since> */
void metrics_job(const char *result);
/* the server's intervals (full firmware, DESIGN.md 5.3.12), kept here across resets and firmware switches: a reset
 * with the server unreachable must not fall back to the 60 min default (test128 2026-11-07: the next check-in an hour
 * away); 0 leaves one as it is / means none yet */
void metrics_intervals_set(uint32_t checkin_s, uint32_t leakcheck_s);
void metrics_intervals(uint32_t *checkin_s, uint32_t *leakcheck_s);

/* before a check-in: AHT20, probe node, BL616 die temperature and supply read now (~70 ms, ADC calibration once per
 * boot ~29 ms) */
void metrics_sample(void);

/* the AHT20 read now (the climate timer, full firmware): rh/t x10, false when it failed (rh -1); the check-in's t/rh
 * from then on */
bool metrics_aht20(int *rh_x10, int *t_x10);
/* `climate sim`: the humidity every read gives instead (x10), -1 = the sensor's again (tests only, DESIGN.md 5.3.15) */
void metrics_rh_sim(int rh_x10);
/* the room (DESIGN.md 5.3.15): a reading taken after the board was cool for the window; in every check-in from then
 * on as troom/rhroom/roomago (kept across resets and the firmware switch) */
void metrics_room(int t_x10, int rh_x10);
/* how long the board has been cool now (no K230, no USB power), s: `quiet` in the check-in (climate.c) */
void metrics_quiet(uint32_t (*fn)(void));

/* the check-in's form, appended after what api.c wrote (&key=value...); returns the new length, or cap on overflow */
size_t metrics_form(char *form, size_t len, size_t cap);

/* the check-in's answer: ok resets the counters it reported, a failure counts */
void metrics_checkin(bool ok);
/* a Wi-Fi change from the server tried: its wifi_check state (WCHK_SAVED: stored), reported once (DESIGN.md 5.3.13) */
void metrics_wifi_change(int state);

#endif
