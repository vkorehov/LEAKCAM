/*
 * The leak checks of the full firmware (DESIGN.md 5.3.12): a wet probe (ACOMP1 below 1.10 V, tests/test034) at once and
 * the RTC check every interval the server gives (default a day) each run a K230 leak check: a new session with that
 * WAKE reason, or CHECK to a K230 already on (USB). The K230's DONE ends it: the result kept for the check-in, a check-in
 * at once on a change or a wet probe, the K230 off on battery. (HBN sleep between the events comes next: the firmware
 * still stays awake.)
 */
#ifndef LEAKCAM_LEAK_CHECK_H
#define LEAKCAM_LEAK_CHECK_H

#include <stdbool.h>
#include <stdint.h>

#include "k230_link.h"

/* once at boot, after the K230 session (k230_session_init) */
void leak_check_start(void);
/* the leak check interval from the server's check-in answer ("check": seconds) */
void leak_check_every(uint32_t s);
/* the K230's DONE,<cam0>,<cam1> (in the session's task) */
void leak_check_done(const struct link_msg *m);
/* a check asked and its DONE not in yet: the session must not be ended */
bool leak_check_busy(void);

#endif
