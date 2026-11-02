/*
 * The LEAKCAM API (the presence server, server/): the check-in both firmwares make. The network check ends there
 * (user, 2026-11-02: "API reachable should be the end goal of the network test", DESIGN.md 5.3.1): the BLE credential
 * check (ble_pairing.c) and the setup firmware's check of the stored network before it switches to the full
 * firmware run the probe, then this check-in; the full firmware's presence check-in (presence.c) is this alone.
 */
#ifndef LEAKCAM_API_H
#define LEAKCAM_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wifi_check.h"

/* the bench server: server/ on the Pi (vhome, systemd unit leakcam-server, server/deploy.sh); the product takes the
 * address from the K230 agent later (DESIGN 5.1) */
#define API_HOST "192.168.1.159"
#define API_PORT "8090"

/* the check-in for this device on the network ssid (GET /v1/poll?id=<id>&ssid=<ssid>: the SSID for the app, never the
 * passphrase), 200 expected; path (>= API_PATH_CAP) and body are the caller's, body NULL: not kept */
#define API_PATH_CAP (32 + 3 * 32 + 1)
void api_poll_target(struct wifi_check_target *t, char *path, const char *ssid, size_t ssid_len, char *body,
                     size_t body_cap);

/* the kept crash record for the server: POST /v1/devices/<id>/crash, the form image=..&ver=..&n=..&k=..&t=..&up=..&c=..
 * &pc=..&v=..&ra=..&sp=..&st=..&bt=<hex,...>; 200 expected; false with no record. path >= API_PATH_CAP, form >=
 * API_CRASH_FORM_CAP; *count: the record's count, for crash_rec_sent() (DESIGN.md 5.3.3) */
#define API_CRASH_FORM_CAP 384
bool api_crash_target(struct wifi_check_target *t, char *path, char *form, uint32_t *count);

#endif
