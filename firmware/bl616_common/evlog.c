#include "evlog.h"
#include "evlog_calc.h"

#include <stdio.h>
#include <string.h>

#include "bflb_mtimer.h"
#include "shell.h"

/* after the other .hbn_noinit records by name ("z"): their addresses stay what both firmwares already agree on */
static struct evlog log_store __attribute__((section(".hbn_noinit.zevlog")));

void evlog_add(enum ev ev, int arg, unsigned val)
{
    evlog_push(&log_store, (uint32_t)bflb_mtimer_get_time_ms(), (uint8_t)ev, (int8_t)arg, val);
}

static const char *const names[] = { "?", "boot", "usb", "sleep", "k230", "adv", "switch", "mark", "WEDGE", "reg" };
/* bl616_setup/blecore.c's points and registers, in its order */
static const char *const reg_points[] = { "?", "boot", "sleep+", "sleep-", "parked", "stuck" };
static const char *const reg_names[] = { "PDS_CTL", "PDS_CTL4", "PDS_CTL5", "DEEPSLCNTL", "BLE_0x100" };
#define N(a) (sizeof(a) / sizeof((a)[0]))

void evlog_print(void)
{
    if (!evlog_intact(&log_store)) {
        printf("evlog: empty (power loss or first boot)\r\n");
        return;
    }
    printf("evlog: %u events, oldest first (ms = uptime since that boot)\r\n", log_store.count);
    for (unsigned i = 0; i < log_store.count; i++) {
        const struct evlog_entry *e = evlog_at(&log_store, i);
        const char *n = e->ev < sizeof(names) / sizeof(names[0]) ? names[e->ev] : "?";
        printf("  %8lu ms  %-6s", (unsigned long)e->ms, n);
        switch (e->ev) {
            case EV_BOOT:   printf(" %s firmware, rst=%02lx", e->arg ? "setup" : "full", (unsigned long)e->val); break;
            case EV_USB:    printf(" %s", e->arg ? "plugged" : "unplugged"); break;
            case EV_SLEEP:  printf(e->arg ? " begins" : " ends, %lu advertising wakes", (unsigned long)e->val); break;
            case EV_K230:   printf(" session %s", e->arg ? "starts" : "stops"); break;
            case EV_ADV:    printf(" %s, %lu ms", e->arg ? "on" : "off", (unsigned long)e->val); break;
            case EV_SWITCH: printf(" to the %s firmware", e->arg ? "setup" : "full"); break;
            case EV_MARK:   printf(" step %d value %lu", e->arg, (unsigned long)e->val); break;
            case EV_WEDGE:
                if (e->arg == 1)
                    printf(" BLE parked: no advertising wake, %lu other wakes", (unsigned long)e->val);
                else
                    printf(" BLE start stuck (init spin)");
                break;
            case EV_REG:
                printf(" %-6s %-10s %08lx", (unsigned)e->arg / 8 < N(reg_points) ? reg_points[e->arg / 8] : "?",
                       (unsigned)e->arg % 8 < N(reg_names) ? reg_names[e->arg % 8] : "?", (unsigned long)e->val);
                break;
            default:        printf(" %d %lu", e->arg, (unsigned long)e->val); break;
        }
        printf("\r\n");
    }
}

static void cmd_evlog(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "clear")) {
        evlog_clear(&log_store);
        printf("evlog: cleared\r\n");
        return;
    }
    evlog_print();
}
SHELL_CMD_EXPORT_ALIAS(cmd_evlog, evlog, the retained event log [clear]);
