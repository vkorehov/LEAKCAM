/*
 * Credential check on USB power, for BLE provisioning: join the network with the new SSID and
 * passphrase, get an address by DHCP and fetch a well-known internet URL, all on the BL616
 * itself. The same run with the server as its target is the presence check-in (presence.c). The K230 is not running meanwhile (wifi_link_claim()), so the BL616 can use the MAC
 * for its own DHCP client. States and codes: wifi_check_calc.h.
 */
#ifndef LEAKCAM_WIFI_CHECK_H
#define LEAKCAM_WIFI_CHECK_H

#include <stddef.h>
#include <stdint.h>

#include "wifi_check_calc.h"

/* the probe: Android's own connectivity check, a 204 with an empty body; anything else is a
 * captive portal or a filtering proxy */
#define WCHK_PROBE_HOST "connectivitycheck.gstatic.com"
#define WCHK_PROBE_PATH "/generate_204"

typedef void (*wifi_check_progress_cb)(enum wifi_check_state state, uint16_t detail);

/* what the run fetches over HTTP once it has an address */
struct wifi_check_target {
    const char *host;                    /* a name or a dotted IP */
    const char *port;                    /* "80" */
    const char *path;                    /* with its query */
    int expect;                          /* the HTTP status that means "reached" */
    char *body;                          /* the answer's body, NUL-terminated (NULL: not kept) */
    size_t body_cap;
};
extern const struct wifi_check_target wifi_check_probe;   /* WCHK_PROBE_HOST / PATH, 204 */

/* Blocking, from a task of its own; claims the radio (WIFI_OWNER_CHECK) for the run. One run at a time:
 * a second caller waits for the first (the BLE check and a presence check-in). Reports each step
 * through progress and returns the last state: WCHK_INTERNET when the target answered with its
 * status, else a failure (with *detail). The station is disconnected and its DHCP client stopped
 * on return. Takes up to about a minute. */
enum wifi_check_state wifi_check_run(const char *ssid, size_t ssid_len, const char *psk, size_t psk_len,
                                     const struct wifi_check_target *t, wifi_check_progress_cb progress,
                                     uint16_t *detail);

#endif
