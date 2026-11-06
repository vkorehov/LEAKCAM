#pragma once

#include <stdbool.h>
#include <stddef.h>

/* The check-in answer's upgrade offer ("full", "full_sha", "full_size", "xz_sha", "xz_size": server/main.go poll):
 * the .xz downloaded into the free end of slot 0 while this firmware runs on (ota_env.h), checked by read-back, kept
 * for the setup firmware. true: ready, switch to the setup firmware with reason "ota"; false: no offer, it does not
 * fit, or the download failed (the next check-in tries again). ssid/psk: the stored network. */
bool ota_fetch(const char *body, const char *ssid, size_t ssid_len, const char *psk, size_t psk_len);
