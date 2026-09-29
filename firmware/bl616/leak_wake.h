#ifndef LEAKCAM_LEAK_WAKE_H
#define LEAKCAM_LEAK_WAKE_H

#include <stdbool.h>
#include <stdint.h>

/* why this boot runs: a bitmask, since several sources can be pending at once and the sensors
 * add their own (a wet probe on an RTC wake is rtc+leak) */
enum wake_reason {
    WAKE_COLD  = 0x01,  /* power-on or reset, not from hibernate */
    WAKE_LEAK  = 0x02,  /* ACOMP1 edge on GPIO20, or the probes read wet */
    WAKE_RTC   = 0x04,  /* scheduled wake-up */
    WAKE_USB   = 0x08,  /* ACOMP0 falling edge on PGOOD: USB plugged in */
    WAKE_HUMID = 0x10,  /* not a hardware source: the humidity reading is at the alarm */
};

void leak_init(void);
bool leak_is_wet(void);
/* probe node voltage in mV (1650 dry, below 825 the comparator calls it wet); -1 = no reading */
int leak_probe_mv(void);
/* the hardware sources pending since the hibernate, WAKE_COLD when none */
unsigned wake_reason_get(void);
/* "leak+rtc" style, into buf; the WAKE frame and the logs use it */
const char *wake_reason_text(unsigned reasons, char *buf, unsigned len);

/* Enter HBN level 0 (~2.1 uA chip). Only GPIO16-19, RTC and ACOMP0/1 can wake HBN on BL616;
 * the leak line is on GPIO20, which is why the comparator is used instead of a pin interrupt.
 * All non-AON pads stop driving in HBN, so the K230 MUST already be off (R19 then holds
 * K230_PWR low). Does not return: wake-up is a reboot. */
void hbn_sleep(uint32_t seconds);

/* persist_get / persist_set moved to aon_state.h (HBN RAM; HBN_RSV0 is overwritten on sleep) */

/* Run the HBN RTC from the 32.768 kHz crystal Y3 (IO16/IO17) instead of RC32K. */
void rtc_use_crystal(void);

#endif
