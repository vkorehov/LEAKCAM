/*
 * Pictures and videos for the server (DESIGN.md 5.3.13, 5.3.14): the check-in answer's "capture" (the app's Take photo
 * / Take video, or the record of a leak) goes to the K230 as SHOOT; the K230 asks for the Wi-Fi bridge (WIFI), takes
 * the pictures, uploads each under its ID and answers SENT. The bridge stops with the session (its rails).
 */
#ifndef LEAKCAM_CAPTURE_H
#define LEAKCAM_CAPTURE_H

#include <stdbool.h>

#include "k230_link.h"

/* after every check-in that reached the server: its answer's "capture", if any, to the K230 (a session started for
 * it, or SHOOT to the running one) */
void capture_answer(const char *body);
/* the K230's WIFI and SENT (in the session's task); true when it was one of them */
bool capture_cmd(const struct link_msg *m);
/* a SHOOT sent and its SENT not in yet: the session must not be ended */
bool capture_busy(void);

#endif
