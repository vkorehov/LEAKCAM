/*
 * The credential check's results and the parts of it that are plain logic (host-tested in
 * test/test_wifi_check.c): how a failed join is read, and the HTTP status line of the internet
 * probe. wifi_check.c does the rest on the SDK.
 *
 * The state is what BLE provisioning reports on its status characteristic (ble_pairing.h), and
 * the Android app reads the same numbers: keep them in step with android/.../MainActivity.kt.
 */
#ifndef LEAKCAM_WIFI_CHECK_CALC_H
#define LEAKCAM_WIFI_CHECK_CALC_H

#include <stddef.h>
#include <stdint.h>

enum wifi_check_state {
    WCHK_IDLE = 0x00,
    WCHK_JOINING = 0x01,          /* looking for the network and joining it */
    WCHK_ADDRESS = 0x02,          /* joined: the password is right; asking DHCP for an address */
    WCHK_INTERNET = 0x03,         /* have an address: resolving and fetching the probe URL */
    WCHK_SAVED = 0x04,            /* the credentials are stored (after a passed check, or unchecked) */
    /* failures: nothing is stored */
    WCHK_NOT_FOUND = 0x10,        /* no such SSID on 2.4 GHz */
    WCHK_WRONG_PASSWORD = 0x11,   /* detail: 802.11 reason code */
    WCHK_JOIN_FAILED = 0x12,      /* detail: 802.11 status code, else reason code */
    WCHK_NO_ADDRESS = 0x13,       /* no DHCP lease */
    WCHK_NO_DNS = 0x14,           /* the probe host did not resolve */
    WCHK_NO_INTERNET = 0x15,      /* resolved, but no TCP connection to it */
    WCHK_CAPTIVE = 0x16,          /* an answer other than 204 (login page, proxy): detail = HTTP status */
    WCHK_BUSY = 0x17,             /* the K230 has the radio (a leak session on USB) */
    WCHK_SAVE_FAILED = 0x18,      /* flash write failed */
};

/* a join that ended without a connection: the network was seen, so either the key did not
 * match or the access point refused for another reason. Wrong keys show as the 4-way handshake
 * failing (802.11 reason 15, its group-key step 16, a MIC failure 14, 802.1X failure 23) or as
 * an SAE/auth challenge failure (status 15). */
static inline enum wifi_check_state wchk_join_failure(uint16_t status_code, uint16_t reason_code, uint16_t *detail)
{
    if (status_code == 15 || reason_code == 14 || reason_code == 15 || reason_code == 16 || reason_code == 23) {
        *detail = status_code == 15 ? status_code : reason_code;
        return WCHK_WRONG_PASSWORD;
    }
    *detail = status_code ? status_code : reason_code;
    return WCHK_JOIN_FAILED;
}

/* the status code of "HTTP/1.x NNN ...", or -1 */
static inline int wchk_http_status(const char *buf, size_t n)
{
    if (n < 12 || buf[0] != 'H' || buf[1] != 'T' || buf[2] != 'T' || buf[3] != 'P' || buf[4] != '/' ||
        buf[5] != '1' || buf[6] != '.' || buf[8] != ' ')
        return -1;
    int code = 0;
    for (int i = 9; i < 12; i++) {
        if (buf[i] < '0' || buf[i] > '9')
            return -1;
        code = code * 10 + (buf[i] - '0');
    }
    return (n == 12 || buf[12] == ' ' || buf[12] == '\r') ? code : -1;
}

#endif
