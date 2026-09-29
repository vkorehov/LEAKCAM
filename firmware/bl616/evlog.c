#include "evlog.h"
#include "evlog_calc.h"

#include <stdio.h>
#include <time.h>

#include "bflb_rtc.h"
#include "bl616_hbn.h"
#include "log.h"
#include "shell.h"

#include "aon_state.h"
#include "leak_wake.h"

/* just below the 64-byte aon block at the end of HBN RAM (aon_state.c). The SDK's own hbn_ram
 * section is at the start and is reloaded at every boot; the check below keeps them apart. */
#define EVLOG_ADDR (HBN_RAM_BASE + HBN_RAM_SIZE - 64u - 512u)
_Static_assert(sizeof(struct evlog) <= 512, "event log must fit its 512 bytes");

extern uint8_t __hbn_ram_end__[];

static struct evlog *const log_ram = (struct evlog *)EVLOG_ADDR;

static uint32_t rtc_s(void)
{
    return (uint32_t)((bflb_rtc_get_time(NULL) & AON_RTC_MASK) / AON_RTC_HZ);
}

void evlog_add(enum ev ev, int arg, unsigned val)
{
    if ((uintptr_t)__hbn_ram_end__ > EVLOG_ADDR)
        return;                         /* the SDK's HBN code grew into the log: never overwrite it */
    evlog_push(log_ram, rtc_s(), (uint8_t)ev, (int8_t)arg, (uint16_t)(val > 0xFFFFu ? 0xFFFFu : val));
}

static const char *ev_name(uint8_t ev)
{
    static const char *const names[] = {
        [EV_BOOT] = "boot", [EV_ENV] = "env", [EV_AHT_FAIL] = "aht20-fail",
        [EV_SESSION] = "session", [EV_READY] = "ready", [EV_NO_READY] = "no-ready",
        [EV_NAK] = "nak", [EV_WIFI] = "wifi", [EV_LINK_LOST] = "link-lost",
        [EV_SESSION_END] = "session-end", [EV_RETRY] = "retry", [EV_SLEEP] = "sleep",
        [EV_USB] = "usb", [EV_CLOCK] = "clock", [EV_CLOCK_LOST] = "clock-lost",
        [EV_RADIO_FAIL] = "radio-fail", [EV_BLE_CREDS] = "ble-creds", [EV_BATTERY] = "battery",
    };
    return ev < sizeof(names) / sizeof(names[0]) && names[ev] ? names[ev] : "?";
}

static void print_detail(const struct evlog_entry *e)
{
    switch (e->ev) {
        case EV_BOOT:
        case EV_SESSION: {
            char why[24];
            printf(" %s", wake_reason_text((uint8_t)e->arg, why, sizeof(why)));
            if (e->ev == EV_BOOT)
                printf(" probe=%u mV", e->val);
            break;
        }
        case EV_ENV:         printf(" %u.%u %%RH %d C", e->val / 10, e->val % 10, e->arg); break;
        case EV_AHT_FAIL:    printf(" result=%d", e->arg); break;
        case EV_READY:       printf(" after %u ms", e->val); break;
        case EV_NO_READY:    printf(" within %u s", e->val); break;
        case EV_NAK:         printf(" %c... code=%d", (char)e->val, e->arg); break;
        case EV_WIFI:        printf(" %s", e->arg ? "start failed" : "started"); break;
        case EV_LINK_LOST:   printf(" after %u sends", e->val); break;
        case EV_SESSION_END: printf(" after %u s", e->val); break;
        case EV_RETRY:       printf(" attempt %d end=%u", e->arg, e->val); break;
        case EV_SLEEP:
            if ((uint8_t)e->arg == 255)
                printf(" %u s, awake >1020 ms", e->val);
            else
                printf(" %u s, awake %u ms", e->val, (uint8_t)e->arg * 4u);
            break;
        case EV_USB:         printf(" %s", e->arg ? "plugged" : "unplugged"); break;
        case EV_CLOCK:       if (e->arg) printf(" correction %d s", (int16_t)e->val); break;
        case EV_BATTERY:     printf(" %u mV", e->val); break;
        default:             break;
    }
}

/* newlib has it; the SDK's headers only declare it with POSIX features on */
struct tm *gmtime_r(const time_t *t, struct tm *out);

void evlog_print(void)
{
    if (!evlog_intact(log_ram)) {
        printf("evlog: empty (power loss or first boot)\r\n");
        return;
    }
    uint32_t now = rtc_s(), epoch;
    bool clock = wallclock_get(&epoch);
    printf("evlog: %u events, oldest first\r\n", log_ram->count);
    for (unsigned i = 0; i < log_ram->count; i++) {
        const struct evlog_entry *e = evlog_at(log_ram, i);
        if (e->t > now) {
            printf("  (before an RTC reset)     ");
        } else if (clock) {
            time_t t = (time_t)(epoch - (now - e->t));
            struct tm tm;
            gmtime_r(&t, &tm);
            printf("  %04d-%02d-%02d %02d:%02d:%02dZ  ", tm.tm_year + 1900, tm.tm_mon + 1,
                   tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        } else {
            printf("  %10lu s ago        ", (unsigned long)(now - e->t));
        }
        printf("%s", ev_name(e->ev));
        print_detail(e);
        printf("\r\n");
    }
}
static void cmd_evlog(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    evlog_print();
}
SHELL_CMD_EXPORT_ALIAS(cmd_evlog, evlog, print the retained event log);
