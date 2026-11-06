#include "usb_full.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "easyflash.h"

#include "battery.h"
#include "device_id.h"
#include "k230_power.h"
#include "k230_session.h"
#include "presence.h"
#include "switch_to_setup.h"
#include "usb_power.h"

/*
 * The cable's commands in the full firmware (the setup firmware's usb_setup.c has the provisioning): uppercase, one
 * line each, answered on the cable:
 *
 *   ID              -> ID <device id> 1 <battery mV, -1 none> <USB power 0|1>   (as the setup firmware: 1 = stored)
 *   PIN <6 digits>  -> OK | ERR PIN | ERR LOCKED                  (the stored pairing PIN; 5 wrong = locked until the
 *                                                                  cable is pulled)
 *   SETUP           -> OK, then the switch to the setup firmware ("wake": the relay there, back in 2 min without a
 *                      setup); before PIN: ERR AUTH
 *   anything else   -> ERR CMD
 *
 * The lines carry the PIN: nothing here prints a line.
 */
#define PIN_TRIES       5
#define POLL_MS         200       /* the USB watch: an unplug seen within it */
#define BATTERY_MS      3000      /* the unplug's reading: STATUS asked -> the K230's STATUS (ADC 64 conversions + the link) */
#define RETRY_MS        30000     /* a session that ended with USB still in (a failure, `k230 off` before `k230 usbboot`
                                     for flash_nand.sh): started again this long after its end */

static bool authed;
static unsigned wrong;
static volatile bool setup_asked;

static void answer(const char *s)
{
    (void)k230_session_usb_line(s);
}

static int stored_pin(void)
{
    char pin[7] = { 0 };
    size_t n = 0;
    (void)ef_get_env_blob("pair_pin", pin, sizeof(pin) - 1, &n);
    return n == 6 && strspn(pin, "0123456789") == 6 ? atoi(pin) : -1;
}

/* in the session's task: the switch waits for the session's end, so it is the watch task's (setup_asked) */
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
        char s[40];
        snprintf(s, sizeof(s), "ID %s 1 %d %d", device_id(), battery_mv(), battery_usb_power() ? 1 : 0);
        answer(s);
    } else if (!strcmp(line, "PIN")) {
        int pin = stored_pin();
        if (wrong >= PIN_TRIES) {
            answer("ERR LOCKED");
        } else if (pin >= 0 && strlen(arg) == 6 && strspn(arg, "0123456789") == 6 && atoi(arg) == pin) {
            authed = true;
            wrong = 0;
            printf("usb_full: phone authenticated\r\n");
            answer("OK");
        } else {
            wrong++;
            printf("usb_full: wrong PIN (%u of %u)\r\n", wrong, PIN_TRIES);
            answer(wrong >= PIN_TRIES ? "ERR LOCKED" : "ERR PIN");
        }
    } else if (!strcmp(line, "SETUP")) {
        if (!authed) {
            answer("ERR AUTH");
            return;
        }
        answer("OK");
        setup_asked = true;
    } else {
        answer("ERR CMD");
    }
}

static void watch_task(void *arg)
{
    (void)arg;
    TickType_t retry_at = 0;
    bool was_on = false, was_usb = usb_present();          /* at boot presence checks in within 5 s anyway */
    for (;;) {
        bool usb = usb_present(), on = k230_session_active();
        if (usb && !was_usb)
            presence_now();                                  /* the plug: charging at the server and the app at once */
        was_usb = usb;
        if (was_on && !on)
            retry_at = xTaskGetTickCount() + pdMS_TO_TICKS(RETRY_MS);
        was_on = on;
        if (setup_asked) {
            setup_asked = false;
            vTaskDelay(pdMS_TO_TICKS(500));                  /* the OK reaches the phone */
            printf("usb_full: the app asked for the setup firmware over the cable\r\n");
            switch_to_setup("wake");                         /* ends the session first; returns only on a failure */
        }
        if (usb && !k230_power_is_on() && xTaskGetTickCount() >= retry_at) {   /* on: a session or `k230 usbboot` */
            authed = false;
            wrong = 0;
            printf("usb_full: USB power: K230 on\r\n");
            k230_session_start("usb", on_line);
        } else if (!usb && k230_session_active()) {
            bool got = k230_session_battery(BATTERY_MS);    /* the cell, read by the K230 already up */
            printf("usb_full: USB unplugged: battery %d mV %s\r\n", battery_mv(), got ? "read now" : "not read");
            k230_session_stop("USB unplugged");
            while (k230_session_active())                    /* the stop is the session task's: asked once */
                vTaskDelay(pdMS_TO_TICKS(50));
            presence_now();                                  /* the new level at the server and the app at once */
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void usb_full_start(void)
{
    if (xTaskCreate(watch_task, "usb_full", 1024, NULL, 4, NULL) != pdPASS)
        vAssertCalled();
}
