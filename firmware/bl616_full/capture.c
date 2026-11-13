#include "capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "bflb_mtimer.h"
#include "shell.h"

#include "api.h"
#include "k230_session.h"
#include "leak_check.h"
#include "presence.h"
#include "usb_full.h"
#include "usb_power.h"
#include "wifi_link.h"

/*
 * The answer's capture (server/alert.go checkInAlerts, Go's map keys in order):
 *   "capture":{"cams":2,"gap_ms":2000,"ids":["<dev>-<unix s>-p1",...],"kind":"leak","photos":4,"secs":15}
 * goes to the K230 as one line (k230_link.h):
 *   SHOOT,<kind>,<dev>-<unix s>,<cams>,<photos>,<gap_ms>,<secs>,<host>,<port>
 * photos: a photo has one per camera, a leak "photos" (2 per camera, gap_ms apart: the water's movement); videos:
 * one per camera (video, leak), secs long. The K230 uploads each to http://<host>:<port>/v1/media/<id> (plain HTTP on
 * the LAN until the K230 has TLS, tests/test109), the IDs numbered as the server's: picture n from camera (n-1) % cams.
 *
 * The K230's WIFI: the bridge (wifi_link_start, NetHub over SDIO) for this session, in a task of its own (the first
 * start takes ~1 s: the session task must keep watching the heartbeat). The check-in's own Wi-Fi check takes the
 * radio meanwhile and hands it back (wifi_link.h): the K230 retries what failed in that window.
 */
#define MEDIA_PORT    "8090"      /* server/main.go Addr: the media routes (the boards' mTLS port 8443 has none) */
#define CLAIM_MS      20000       /* a check-in's Wi-Fi check holds the radio a few s at most */
#define SENT_MAX_MS   120000      /* on battery: no SENT by then = the capture failed, the K230 off (videos 2 x 15 s
                                   * + the uploads) */

static volatile bool busy, bridged, bridging, more;   /* bridging: its task starting it; more: another waits */
static uint64_t busy_since;
static unsigned asked, sent_ok, sent_fail;
static char last[48];
static TimerHandle_t guard;

/* a number after "<key>": inside the capture object; -1 when absent */
static long field(const char *c, const char *key)
{
    char k[16];
    snprintf(k, sizeof(k), "\"%s\":", key);
    const char *p = strstr(c, k);
    return p ? strtol(p + strlen(k), NULL, 10) : -1;
}

static void stop_if_battery(const char *why)
{
    if (!usb_present())
        k230_session_stop(why);          /* on battery the K230 only for its job (DESIGN.md 5.3.12) */
}

/* the capture's end: the bridge lives only for it (DESIGN.md 5.3.13: the bridge's association is the session's). The
 * BL616's own check-in and the K230's bridge on one radio and one MAC lost the check-in's frames (presence.c), so the
 * session ends, its rails take the bridge down; on USB the K230 comes back at once (usb_full_again) */
static void end_session(const char *why)
{
    if (!bridged) {
        stop_if_battery(why);
        return;
    }
    if (usb_present())
        usb_full_again();
    k230_session_stop(why);
}

static void guard_fired(TimerHandle_t t)
{
    (void)t;
    if (!busy)
        return;
    printf("capture: no SENT in time after SHOOT: failed\r\n");
    busy = false;
    sent_fail++;
    end_session("capture: no SENT");
}

void capture_answer(const char *body)
{
    const char *c = strstr(body, "\"capture\":{");
    if (!c) {
        /* a leak's check kept the K230 on for this answer (leak_check_done): nothing to capture, off on battery */
        if (!busy && !leak_check_busy() && k230_session_active())
            stop_if_battery("check-in answered: nothing to capture");
        return;
    }
    const char *kind = strstr(c, "\"kind\":\"photo\"") ? "photo" : strstr(c, "\"kind\":\"video\"") ? "video"
                     : strstr(c, "\"kind\":\"leak\"")  ? "leak"
                     : strstr(c, "\"kind\":\"focus\"") ? "focus"      /* the app's focus adjustment (5.3.17) */
                     : strstr(c, "\"kind\":\"ignore\"") ? "ignore" : NULL; /* the cameras to ignore (5.3.18): no
                                                                             * picture, their mask in "photos" */
    const char *id = strstr(c, "\"ids\":[\"");
    long cams = field(c, "cams");
    if (!kind || !id || cams < 1 || cams > 2) {
        printf("capture: answer not understood, dropped\r\n");
        return;
    }
    /* "<dev>-<unix s>" of the first ID: everything before its last '-' */
    id += strlen("\"ids\":[\"");
    const char *q = strchr(id, '"');
    const char *dash = q ? q : id;
    while (dash > id && *dash != '-')
        dash--;
    if (!q || dash == id || dash - id > 32) {
        printf("capture: IDs not understood, dropped\r\n");
        return;
    }
    more = field(c, "more") > 0;         /* another capture waits at the server: a check-in again after SENT */
    long photos = !strcmp(kind, "photo") ? cams : !strcmp(kind, "leak") || !strcmp(kind, "ignore") ? field(c, "photos") : 0;
    long gap = field(c, "gap_ms"), secs = field(c, "secs");
    char arg[LINK_MAX_LINE - 16];
    snprintf(arg, sizeof(arg), "%s,%.*s,%ld,%ld,%ld,%ld,%s,%s", kind, (int)(dash - id), id, cams,
             photos > 0 ? photos : 0, gap > 0 ? gap : 0, secs > 0 ? secs : 0, API_HOST, MEDIA_PORT);
    snprintf(last, sizeof(last), "%s %.*s", kind, (int)(dash - id), id);
    asked++;
    busy = true;
    busy_since = bflb_mtimer_get_time_ms();
    if (!guard)
        guard = xTimerCreate("cap", pdMS_TO_TICKS(SENT_MAX_MS), pdFALSE, NULL, guard_fired);
    if (guard)                           /* a focus session runs up to its secs (10 min): its SENT comes after them */
        xTimerChangePeriod(guard, pdMS_TO_TICKS(!strcmp(kind, "focus") && secs > 0 ? (uint32_t)secs * 1000u + 60000u
                                                                                      : SENT_MAX_MS), 0);
    printf("capture: %s for the server (%ld pictures, %ld cameras)%s\r\n", last, photos, cams,
           k230_session_active() ? ": SHOOT to the running K230" : ": K230 session");
    k230_session_shoot(arg);             /* sent once the K230 has its WAKE */
    if (!k230_session_active())
        k230_session_start(kind, usb_full_cmd);
}

static void bridge_task(void *arg)
{
    (void)arg;
    uint64_t t0 = bflb_mtimer_get_time_ms();
    while (!wifi_link_claim(WIFI_OWNER_K230) && bflb_mtimer_get_time_ms() - t0 < CLAIM_MS)
        vTaskDelay(pdMS_TO_TICKS(100));  /* a check-in's Wi-Fi check has the radio */
    int r = -1;
    if (k230_session_active()) {
        r = wifi_link_start();
        bridged = r == 0;
    }
    if (!bridged)
        wifi_link_release(WIFI_OWNER_K230);   /* the session ended meanwhile, or the start failed */
    bridging = false;
    printf("capture: Wi-Fi bridge for the K230: %s in %lu ms\r\n", r == 0 ? "up" : "failed",
           (unsigned long)(bflb_mtimer_get_time_ms() - t0));
    vTaskDelete(NULL);
}

void k230_session_rails_going(void)
{
    if (bridged) {
        bridged = false;
        wifi_link_stop();                /* before the rails (wifi_link.h) */
        /* the station leaves with the K230: kept associated, the router's frames for the gone K230 went to a dead
         * SDIO link ("sdio2 not ready") and the next own check-in started from the stale association: a lease at once,
         * no TCP (0x15), 3 in a row = the setup firmware (test143 --cold, 2026-11-12) */
        (void)wifi_link_sta_leave();
        wifi_link_release(WIFI_OWNER_K230);
    }
}

bool capture_cmd(const struct link_msg *m)
{
    if (!strcmp(m->cmd, "WIFI")) {
        /* up already (a second SHOOT in this session): nothing to do. Not wifi_link_start() again: its SDIO "restore"
         * on the running bridge reset both NetHub channels and the data channel stayed down ("NETDEV_STA Device not
         * in RUN state, dropping RX frame"), the K230's uploads failed for 2 min (test143, 2026-11-12). A new K230
         * boot is always a new session: the bridge stopped with the rails */
        if (!bridged && !bridging) {
            bridging = true;
            if (xTaskCreate(bridge_task, "bridge", 1024, NULL, 3, NULL) != pdPASS) {
                bridging = false;
                printf("capture: Wi-Fi bridge task not created\r\n");
            }
        }
        return true;
    }
    if (!strcmp(m->cmd, "SENT")) {       /* SENT,<uploaded>,<failed>,<skipped: the videos, not built yet> */
        unsigned ok = 0, fail = 0, skipped = 0;
        (void)sscanf(m->args, "%u,%u,%u", &ok, &fail, &skipped);
        sent_ok += ok;
        sent_fail += fail;
        printf("capture: SENT %u uploaded, %u failed, %u skipped, %lu ms after the ask\r\n", ok, fail, skipped,
               busy ? (unsigned long)(bflb_mtimer_get_time_ms() - busy_since) : 0UL);
        busy = false;
        if (guard)
            xTimerStop(guard, 0);
        end_session("capture sent: the Wi-Fi bridge off");
        if (more)
            presence_now();              /* the next waiting capture now, not at the next interval */
        return true;
    }
    return false;
}

bool capture_busy(void)
{
    return busy;
}

/* `capture`: the state */
static void cmd_capture(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("capture: asked %u, uploaded %u, failed %u, last %s%s, bridge %s\r\n", asked, sent_ok, sent_fail,
           last[0] ? last : "none", busy ? ", one running" : "", bridged ? "up" : "off");
}
SHELL_CMD_EXPORT_ALIAS(cmd_capture, capture, pictures and videos for the server: the state);
