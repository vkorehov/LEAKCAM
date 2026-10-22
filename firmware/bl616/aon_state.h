/*
 * State that must survive hibernate and software resets: the persisted session flags, the
 * wall clock, the last battery reading and the ADC calibration. Kept in HBN RAM (4 KB always-on SRAM, held in
 * retention through HBN level 0; checked on rev 1 in tests/test021) in the linker section .hbn_noinit
 * (bl616_leakcam.ld.in), with a magic and a CRC.
 *
 * Why not HBN_Set_Status_Flag(): it is HBN_RSV0, and pm_hbn_mode_enter() overwrites HBN_RSV0 with
 * its own HBN_STATUS_ENTER_FLAG on the way into hibernate, so anything stored there is lost at
 * every sleep. RSV1 holds the SDK's wake callback, RSV3 is used by the ROM patch code.
 *
 * The clock is the HBN RTC counter (Y3 32.768 kHz crystal, keeps counting through hibernate and
 * software resets; pm_hbn_mode_enter() only adds a compare value, it does not clear the counter)
 * plus a reference pair (Unix seconds, RTC count). It is lost only when 3V3_SLEEP goes away
 * (battery out) or the chip is reset through EN; then clock_valid is 0 until the K230 sends a
 * real time (TIME frame, from NTP) again. Drift is the crystal's, about +-20 ppm = 1.7 s/day.
 */
#ifndef LEAKCAM_AON_STATE_H
#define LEAKCAM_AON_STATE_H

#include <stdbool.h>
#include <stdint.h>

/* call first at boot, before any HBN RAM access: takes HBN RAM out of retention mode, validates the block (fresh
 * after a power loss) and rebases the clock onto the current RTC count */
void aon_init(void);

void persist_get(uint32_t *flags, uint32_t *hum_wakes_left);
void persist_set(uint32_t flags, uint32_t hum_wakes_left);

/* false = time unknown since the last power loss */
bool wallclock_get(uint32_t *epoch);
/* false = refused (before 2026-01-01, i.e. not a real time) */
bool wallclock_set(uint32_t epoch);

/* battery voltage as the K230 last measured it (ACK of WAKE); -1 = none since the power loss */
int battery_get(void);
void battery_set(unsigned mv);

/* the GPADC calibration bflb_adc_init() runs once per reset (28.6 ms, tests/test021), kept across HBN:
 * coe, os1, os2, tsen_offset of bflb_adc.c; false = none since the power loss */
struct adc_cal {
    float coe;
    uint32_t os1;
    int32_t os2;
    uint32_t tsen;
};
bool adc_cal_get(struct adc_cal *c);
void adc_cal_set(const struct adc_cal *c);

/* the last call before pm_hbn_mode_enter(): HBN RAM into retention; no HBN RAM access after it (in RET an access
 * after an AON register write is corrupted, tests/test021) */
void aon_prepare_sleep(void);

#endif
