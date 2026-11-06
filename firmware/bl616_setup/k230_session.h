/*
 * A K230 session in the setup firmware: the K230 powered for one job, which the WAKE reason names (DESIGN.md 5.3.8).
 * Today only `setup`: the USB setup relay (usb_setup.c). The BL616 owns the power: the session ends when the K230's
 * heartbeat stops, when it never starts, when the link fails, or on k230_session_stop(); every end is logged with its
 * reason and the rails go off (battery).
 */
#ifndef LEAKCAM_K230_SESSION_H
#define LEAKCAM_K230_SESSION_H

#include <stdbool.h>

#include "k230_link.h"

/* the session task, once at boot (after the USB console) */
void k230_session_init(void);
/* power the K230 for `reason`; on_cmd gets the K230's commands other than READY (USB,<line>) in the session's task */
void k230_session_start(const char *reason, void (*on_cmd)(const struct link_msg *m));
/* end it (from any task): the rails off, `why` logged */
void k230_session_stop(const char *why);
bool k230_session_active(void);
/* a line for the phone (USB,<line>), queued and sent once the K230 has its WAKE; false: queue full (the caller's
 * line is dropped and logged) */
bool k230_session_usb_line(const char *line);

#endif
