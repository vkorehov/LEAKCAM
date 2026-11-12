/*
 * The pictures for the server (DESIGN.md 5.3.13, 5.3.14): the BL616's SHOOT (k230_link.h) carries the check-in
 * answer's capture; this thread asks for the Wi-Fi bridge (WIFI), takes one lit colour frame per camera per round
 * (job_colour: the leak's rounds gap_ms apart, so the water's movement shows), encodes each as a JPEG with the moment
 * in EXIF (jpeg.c), uploads it under its ID with X-Taken, and hands the count back for SENT. The relay thread owns
 * the link: it sends WIFI and SENT for this thread.
 */
#ifndef LEAKCAM_MEDIA_H
#define LEAKCAM_MEDIA_H

#include <stdbool.h>
#include <stddef.h>

/* the thread, waiting for its first SHOOT */
void media_start(void);
/* SHOOT,<kind>,<dev>-<unix s>,<cams>,<photos>,<gap_ms>,<secs>,<host>,<port>: queued (one at a time; a second while
 * one runs follows it) */
void media_request(const char *arg);
/* the relay: true when the thread waits for the bridge (send WIFI, then media_wifi_answer) */
bool media_wifi_wanted(void);
void media_wifi_answer(bool acked);
/* the relay: a finished request's "<uploaded>,<failed>,<skipped>" for SENT; true once per request */
bool media_result(char *buf, size_t len);

#endif
