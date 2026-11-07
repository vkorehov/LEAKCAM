/*
 * A K230 session: the K230 powered for one job, which the WAKE reason names (DESIGN.md 5.3.8). Both firmwares hold
 * one for as long as USB power stays (DESIGN.md 5.3.11): `setup` in the setup firmware (the USB setup relay,
 * usb_setup.c), `usb` in the full firmware (the relay too, usb_full.c: the app reaches the board over the cable in
 * either firmware). The BL616 owns the power: the session ends when the K230's heartbeat stops, when it never starts,
 * when the link fails, or on k230_session_stop(); every end is logged with its reason and the rails go off.
 */
#ifndef LEAKCAM_K230_SESSION_H
#define LEAKCAM_K230_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "k230_link.h"

/* the session task, once at boot (after the USB console) */
void k230_session_init(void);
/* power the K230 for `reason`; on_cmd gets the K230's commands other than READY (USB,<line>) in the session's task */
void k230_session_start(const char *reason, void (*on_cmd)(const struct link_msg *m));
/* end it (from any task): the rails off, `why` logged */
void k230_session_stop(const char *why);
bool k230_session_active(void);
/* a leak check by the running K230 (CHECK,<why>, k230_link.h; full firmware): sent once the K230 has its WAKE */
void k230_session_check(const char *why);
/* the battery now (the unplug, DESIGN.md 5.3.11): STATUS asked of the K230, which reads its ADC and sends its STATUS
 * frame at once, the reading on it (kept by battery.c); true once that STATUS came within ms */
bool k230_session_battery(uint32_t ms);
/* a line for the phone (USB,<line>), queued and sent once the K230 has its WAKE; false: queue full (the caller's
 * line is dropped and logged) */
bool k230_session_usb_line(const char *line);

#endif
