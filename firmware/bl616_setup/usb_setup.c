/*
 * Setup over the USB cable (DESIGN.md 5.3.4, 5.3.8; tests/test113): the phone app on USB-C 1, the K230 relaying its
 * lines (WAKE setup, k230_app/leakcam.c), the provisioning the same as over BLE (provision.c). One line per command,
 * commands and answer keywords uppercase, values hex or decimal:
 *
 *   ID                       -> ID <device id> <network stored 0|1>        (no PIN needed: the ID is on the label)
 *   PIN <6 digits>           -> OK | ERR PIN | ERR LOCKED                  (the server's PIN; 5 wrong = locked until
 *                                                                           the cable is pulled)
 *   SSID <hex>               -> OK | ERR SSID                              (after PIN; hex: any byte, nothing escaped)
 *   PSK <hex>                -> OK | ERR PSK                               (empty = an open network)
 *   COMMIT <1|2> <attempt>   -> OK | ERR COMMIT                            (1 check then store, 2 store as is)
 *   STATUS                   -> STATUS <state> <detail> <attempt>          (also sent by itself on every change)
 *   anything else            -> ERR CMD; before PIN: ERR AUTH
 *
 * STATUS lines are pushed whenever the state changes, also in the middle of a command: after COMMIT the first STATUS
 * comes before its OK (2026-11-05). A reader takes STATUS by its keyword, never as the reply to its last command.
 *
 * The lines carry the credentials: nothing here prints a line.
 */
#include "usb_setup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "easyflash.h"

#include "ble_pairing.h"
#include "device_id.h"
#include "k230_session.h"
#include "provision.h"

#define PIN_TRIES 5

static bool authed;
static unsigned wrong;

static void answer(const char *s)
{
    (void)k230_session_usb_line(s);
}

static void status_line(const uint8_t st[PROVISION_STATUS_LEN])
{
    char s[48];
    uint32_t a = st[3] | st[4] << 8 | st[5] << 16 | (uint32_t)st[6] << 24;
    snprintf(s, sizeof(s), "STATUS %u %u %lu", st[0], st[1] | st[2] << 8, (unsigned long)a);
    answer(s);
}

static void status_changed(const uint8_t st[PROVISION_STATUS_LEN])
{
    if (authed && k230_session_active())
        status_line(st);
}

static int hexval(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* hex -> bytes in out (cap); the length, or -1: odd, not hex or too long */
static int unhex(const char *h, char *out, size_t cap)
{
    size_t n = strlen(h);
    if (n % 2 || n / 2 > cap)
        return -1;
    for (size_t i = 0; i < n / 2; i++) {
        int a = hexval(h[2 * i]), b = hexval(h[2 * i + 1]);
        if (a < 0 || b < 0)
            return -1;
        out[i] = (char)(a << 4 | b);
    }
    return (int)(n / 2);
}

static void on_line(const struct link_msg *m)
{
    if (strcmp(m->cmd, "USB"))
        return;
    char line[LINK_MAX_LINE];
    snprintf(line, sizeof(line), "%s", m->args);
    char *arg = strchr(line, ' ');
    if (arg)
        *arg++ = 0;
    else
        arg = line + strlen(line);

    if (!strcmp(line, "ID")) {
        size_t saved = 0;
        char s[40];
        (void)ef_get_env_blob("wifi_ssid", NULL, 0, &saved);
        snprintf(s, sizeof(s), "ID %s %d", device_id(), saved ? 1 : 0);
        answer(s);
        return;
    }
    if (!strcmp(line, "PIN")) {
        int pin = ble_pairing_pin();
        if (wrong >= PIN_TRIES) {
            answer("ERR LOCKED");
        } else if (pin >= 0 && strlen(arg) == 6 && strspn(arg, "0123456789") == 6 && atoi(arg) == pin) {
            authed = true;
            wrong = 0;
            printf("usb_setup: phone authenticated\r\n");
            answer("OK");
        } else {
            wrong++;
            printf("usb_setup: wrong PIN (%u of %u)\r\n", wrong, PIN_TRIES);
            answer(wrong >= PIN_TRIES ? "ERR LOCKED" : "ERR PIN");
        }
        return;
    }
    if (!authed) {
        answer("ERR AUTH");
        return;
    }
    char buf[64];
    int n;
    if (!strcmp(line, "SSID")) {
        n = unhex(arg, buf, 32);
        answer(n > 0 && !provision_ssid(buf, (size_t)n) ? "OK" : "ERR SSID");
    } else if (!strcmp(line, "PSK")) {
        n = unhex(arg, buf, 63);
        answer(n >= 0 && !provision_psk(buf, (size_t)n) ? "OK" : "ERR PSK");
        memset(buf, 0, sizeof(buf));
    } else if (!strcmp(line, "COMMIT")) {
        char *end;
        unsigned long cmd = strtoul(arg, &end, 10), id = strtoul(end, NULL, 10);
        answer(!provision_commit((uint8_t)cmd, (uint32_t)id) ? "OK" : "ERR COMMIT");
    } else if (!strcmp(line, "STATUS")) {
        uint8_t st[PROVISION_STATUS_LEN];
        provision_status(st);
        status_line(st);
    } else {
        answer("ERR CMD");
    }
}

void usb_setup_init(void)
{
    provision_on_status(status_changed);
}

void usb_setup_begin(void)
{
    if (k230_session_active())
        return;
    if (ble_pairing_pin() < 0) {
        printf("usb_setup: no pairing PIN stored: no cable setup\r\n");
        return;
    }
    authed = false;
    wrong = 0;
    provision_clear();
    printf("usb_setup: USB power, setup over the cable: K230 on\r\n");
    k230_session_start("setup", on_line);
}

void usb_setup_end(const char *why)
{
    k230_session_stop(why);
}

bool usb_setup_active(void)
{
    return k230_session_active();
}
