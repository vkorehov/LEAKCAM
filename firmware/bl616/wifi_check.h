/*
 * Credential check on USB power, for BLE provisioning: join the network with the new SSID and
 * passphrase, get an address by DHCP and fetch a well-known internet URL, all on the BL616
 * itself. The K230 is not running meanwhile (wifi_link_claim()), so the BL616 can use the MAC
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

/* Blocking, from a task of its own, with the radio claimed (WIFI_OWNER_CHECK). Reports each
 * step through progress and returns the last state: WCHK_INTERNET when the internet answered,
 * else a failure (with *detail). The station is disconnected and its DHCP client stopped on
 * return. Takes up to about a minute. */
enum wifi_check_state wifi_check_run(const char *ssid, size_t ssid_len, const char *psk, size_t psk_len,
                                     wifi_check_progress_cb progress, uint16_t *detail);

#endif
