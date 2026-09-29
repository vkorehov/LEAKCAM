/*
 * Retained event log: the last EVLOG_N (32) things the BL616 decided or saw, kept in HBN RAM
 * through hibernate and software resets, so the wakes nobody watched (bare-metal battery wakes,
 * sessions in the field) can be read afterwards. Lost with 3V3_SLEEP, like the wall clock.
 *
 * Read it on USB power: connect PR1 (USB CDC console) and type `evlog` in the terminal.
 */
#ifndef LEAKCAM_EVLOG_H
#define LEAKCAM_EVLOG_H

#include <stdint.h>

enum ev {
    EV_BOOT = 1,        /* arg = wake reason, val = probe mV */
    EV_ENV,             /* arg = temperature C, val = RH x10 */
    EV_AHT_FAIL,        /* arg = aht20_result */
    EV_SESSION,         /* K230 powered; arg = wake reason */
    EV_READY,           /* val = ms from power-on to READY */
    EV_NO_READY,        /* val = the timeout, s */
    EV_NAK,             /* arg = NAK code, val = command (first letter) */
    EV_WIFI,            /* arg = 0 started, -1 start failed */
    EV_LINK_LOST,       /* WAKE never answered; val = sends */
    EV_SESSION_END,     /* heartbeat stopped; val = session length, s */
    EV_RETRY,           /* arg = attempt, val = session_end */
    EV_SLEEP,           /* hibernate; arg = 1 probes wet, val = seconds (65535 = longer) */
    EV_USB,             /* arg = 1 plugged (USB mode), 0 unplugged */
    EV_CLOCK,           /* TIME taken; arg = 1 had a clock, val = correction s (int16) */
    EV_CLOCK_LOST,      /* RTC counter reset: wall clock unknown until the next TIME */
    EV_RADIO_FAIL,      /* RF calibration failed: no BLE, no Wi-Fi */
    EV_BLE_CREDS,       /* Wi-Fi credentials stored over BLE */
    EV_BATTERY,         /* the K230's battery reading, in its answer to WAKE; val = mV */
};

void evlog_add(enum ev ev, int arg, unsigned val);

#endif
