#ifndef LEAKCAM_LEAK_WAKE_H
#define LEAKCAM_LEAK_WAKE_H

#include <stdbool.h>
#include <stdint.h>

enum wake_reason {
    WAKE_COLD = 0,      /* power-on or reset, not from hibernate */
    WAKE_LEAK,          /* ACOMP1 edge on GPIO20 */
    WAKE_RTC,           /* scheduled wake-up */
};

void leak_init(void);
bool leak_is_wet(void);
enum wake_reason wake_reason_get(void);
const char *wake_reason_name(enum wake_reason r);

/* Enter HBN level 0 (~2.1 uA chip). Only GPIO16-19, RTC and ACOMP0/1 can wake HBN on BL616;
 * the leak line is on GPIO20, which is why the comparator is used instead of a pin interrupt.
 * All non-AON pads stop driving in HBN, so the K230 MUST already be off (R19 then holds
 * K230_PWR low). Does not return: wake-up is a reboot. */
void hbn_sleep(uint32_t seconds);

/* 32-bit word kept by HBN across hibernate (HBN_Set_Status_Flag) */
uint32_t persist_get(void);
void persist_set(uint32_t v);

#endif
