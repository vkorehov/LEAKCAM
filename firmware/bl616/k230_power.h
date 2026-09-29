#ifndef LEAKCAM_K230_POWER_H
#define LEAKCAM_K230_POWER_H

#include <stdbool.h>
#include <stdint.h>

void k230_power_init(void);                 /* safe state: rails off, reset asserted, pins parked */
void k230_power_on(void);                   /* blocks ~5 ms for the sequence, returns with reset released */
void k230_power_off(void);                  /* hard off: park pins, assert reset, drop K230_PWR,
                                               then wait until 1V8/3V3 are discharged */
bool k230_alive_level(void);                /* IO01: the agent's heartbeat, meaningful after READY only */
void k230_pins_park(void);
void k230_link_pins_attach(void);           /* route UART0 onto GPIO21/22, only while 3V3 is up */

#endif
