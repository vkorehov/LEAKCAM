#ifndef LEAKCAM_K230_POWER_H
#define LEAKCAM_K230_POWER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Minimum time the K230 rails must stay off before the next power-up.
 * 0V8 and 1V1 (TPS62823) discharge actively in < 1 ms, but 1V8 and 3V3 (TPS63802) have no
 * output discharge and decay only through what is still hanging on them. firmware/sim shows
 * 1V8/3V3 below 10 % after 0.6-3.7 s depending on the unknown off-state load; powering up
 * earlier brings 0V8 up AFTER 1V8/3V3, which the K230 guide forbids.
 * 5000 ms covers the light-load case. With 1 k bleed resistors on 1V8 and 3V3 this can be 300 ms.
 */
#ifndef K230_MIN_OFF_MS
#define K230_MIN_OFF_MS 5000
#endif

enum k230_result {
    K230_OK = 0,
    K230_ERR_RAIL_NOT_DISCHARGED,   /* 3V3 still above VIL (~1 V) after the wait */
    K230_ERR_RAIL_NOT_UP,           /* 3V3 never rose after K230_PWR went high */
};

void k230_power_init(void);                 /* safe state: rails off, reset asserted, pins parked */
enum k230_result k230_power_on(void);       /* blocks for the sequence, returns with reset released */
void k230_power_off(void);                  /* hard off: park pins, assert reset, drop K230_PWR */
void k230_hard_reset(void);                 /* RSTN pulse, rails stay on */
bool k230_is_on(void);
bool k230_alive_level(void);                /* raw IO01: heartbeat, or 3V3 presence while K230 is off */
void k230_pins_park(void);
void k230_link_pins_attach(void);           /* route UART0 onto GPIO21/22, only while 3V3 is up */

#endif
