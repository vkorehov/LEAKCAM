/*
 * LEAKCAM BL616 power manager, proof of concept.
 *
 * The BL616 is the always-on part (3V3_SLEEP). It owns the K230's power: it wakes on a leak
 * (ACOMP1 on GPIO20) or on its RTC, powers the K230 up in the order the K230 guide requires,
 * lets it take pictures, and cuts it again when the K230 says it is done or stops responding.
 * Between sessions it hibernates (HBN level 0), which also forces the K230 off: HBN releases
 * every non-AON pad and R19 pulls K230_PWR low.
 *
 * Production note: the BL616 is also the K230's Wi-Fi (SDIO, examples/wifi/sdio_wifi +
 * nethub host_linux). There is only one BL616 firmware, so this logic has to move into that
 * application as a task; it is kept standalone here to exercise the power paths in isolation.
 */
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "bflb_mtimer.h"
#include "log.h"

#include "k230_power.h"
#include "k230_link.h"
#include "leak_wake.h"

#define BOOT_TIMEOUT_MS      90000   /* SPI NAND boot + Linux + agent start; measure and tighten */
#define HEARTBEAT_TIMEOUT_MS 10000   /* agent toggles K230 GPIO2 every 500 ms */
#define SESSION_MAX_MS       300000
#define HALT_TIMEOUT_MS      20000
#define HALT_GRACE_MS        500     /* after HALTED: let the last NAND program finish */
#define BOOT_RETRIES         3

#define REPORT_PERIOD_S      (6u * 3600u)
#define RETRY_PERIOD_S       300u
#define GIVE_UP_PERIOD_S     3600u

/* persisted across hibernate (16 bits) */
#define PF_FAILS_MASK        0x000Fu
#define PF_LEAK_REPORTED     0x0010u

enum session_end {
    END_SLEEP_REQUEST,   /* K230 finished and asked to sleep */
    END_SESSION_TIMEOUT,
    END_HUNG,            /* heartbeat lost after READY */
    END_NO_BOOT,         /* READY never arrived */
    END_POWER_FAULT,
};

static bool wait_for(const char *cmd, uint32_t timeout_ms, struct link_msg *m)
{
    uint64_t t0 = bflb_mtimer_get_time_ms();
    while (bflb_mtimer_get_time_ms() - t0 < timeout_ms) {
        if (link_poll(m) && strcmp(m->cmd, cmd) == 0)
            return true;
        bflb_mtimer_delay_ms(2);
    }
    return false;
}

static void graceful_off(void)
{
    struct link_msg m;
    link_send("SHUTDOWN", NULL);
    if (wait_for("HALTED", HALT_TIMEOUT_MS, &m))
        bflb_mtimer_delay_ms(HALT_GRACE_MS);
    else
        LOG_W("session: no HALTED within %u ms, cutting power anyway\r\n", HALT_TIMEOUT_MS);
    k230_power_off();
}

static enum session_end run_session(enum wake_reason reason, uint32_t *sleep_s)
{
    struct link_msg m;
    enum k230_result pr = k230_power_on();
    if (pr != K230_OK)
        return END_POWER_FAULT;

    link_reset();
    if (!wait_for("READY", BOOT_TIMEOUT_MS, &m)) {
        LOG_E("session: no READY within %u ms\r\n", BOOT_TIMEOUT_MS);
        k230_power_off();                       /* nothing to sync with, K230 never came up */
        return END_NO_BOOT;
    }
    link_send("WAKE", wake_reason_name(reason));

    uint64_t start = bflb_mtimer_get_time_ms(), last_edge = start;
    bool level = k230_alive_level();
    bool reset_tried = false;

    while (1) {
        uint64_t now = bflb_mtimer_get_time_ms();
        bool l = k230_alive_level();
        if (l != level) {
            level = l;
            last_edge = now;
        }

        if (link_poll(&m)) {
            if (strcmp(m.cmd, "SLEEP") == 0) {
                *sleep_s = m.has_arg ? m.arg : REPORT_PERIOD_S;
                link_send("ACK", "SLEEP");
                /* the agent syncs and remounts read-only, then sends HALTED */
                if (wait_for("HALTED", HALT_TIMEOUT_MS, &m))
                    bflb_mtimer_delay_ms(HALT_GRACE_MS);
                k230_power_off();
                return END_SLEEP_REQUEST;
            }
            if (strcmp(m.cmd, "READY") == 0)    /* agent restarted inside the session */
                link_send("WAKE", wake_reason_name(reason));
        }

        if (now - last_edge > HEARTBEAT_TIMEOUT_MS) {
            LOG_E("session: heartbeat lost for %u ms\r\n", (unsigned)(now - last_edge));
            /* First recovery is a reset with the rails left on: no discharge wait, K230 reboots at
             * once. Only if that does not bring the agent back is the chain power-cycled, which
             * costs K230_MIN_OFF_MS before the retry can start. */
            if (!reset_tried) {
                reset_tried = true;
                k230_hard_reset();
                link_reset();
                if (wait_for("READY", BOOT_TIMEOUT_MS, &m)) {
                    link_send("WAKE", wake_reason_name(reason));
                    last_edge = bflb_mtimer_get_time_ms();
                    level = k230_alive_level();
                    continue;
                }
                LOG_E("session: no READY after reset\r\n");
            }
            k230_power_off();                   /* a hung kernel cannot sync; hard off */
            return END_HUNG;
        }
        if (now - start > SESSION_MAX_MS) {
            LOG_W("session: %u s limit reached\r\n", SESSION_MAX_MS / 1000);
            graceful_off();
            return END_SESSION_TIMEOUT;
        }
        bflb_mtimer_delay_ms(5);
    }
}

int main(void)
{
    board_init();
    /* first thing after clocks: rails off, reset held, K230-side pins parked. R19/R76 already
     * hold this state in hardware while the BL616 is in reset or booting. */
    k230_power_init();

    enum wake_reason reason = wake_reason_get();
    leak_init();
    uint32_t pf = persist_get();
    bool wet = leak_is_wet();
    LOG_I("boot: wake=%s probes=%s fails=%u\r\n", wake_reason_name(reason), wet ? "wet" : "dry",
          (unsigned)(pf & PF_FAILS_MASK));

    /* a leak that is still present after an RTC wake is reported once, not every period */
    if (reason == WAKE_LEAK && !wet)
        reason = WAKE_COLD;                     /* edge came from drying out */
    if (reason == WAKE_RTC && wet && !(pf & PF_LEAK_REPORTED))
        reason = WAKE_LEAK;

    uint32_t sleep_s = REPORT_PERIOD_S;
    enum session_end end = END_NO_BOOT;
    unsigned fails = pf & PF_FAILS_MASK;

    for (unsigned attempt = 0; attempt < BOOT_RETRIES; attempt++) {
        end = run_session(reason, &sleep_s);
        if (end == END_SLEEP_REQUEST || end == END_SESSION_TIMEOUT)
            break;
        LOG_W("session: attempt %u ended with %d, retrying\r\n", attempt + 1, (int)end);
        /* the next k230_power_on() waits K230_MIN_OFF_MS itself */
    }

    if (end == END_SLEEP_REQUEST || end == END_SESSION_TIMEOUT) {
        fails = 0;
        if (reason == WAKE_LEAK)
            pf |= PF_LEAK_REPORTED;
        if (!wet)
            pf &= ~PF_LEAK_REPORTED;
    } else {
        fails = fails < PF_FAILS_MASK ? fails + 1 : fails;
        sleep_s = fails >= 3 ? GIVE_UP_PERIOD_S : RETRY_PERIOD_S;
    }
    persist_set((pf & ~PF_FAILS_MASK) | fails);

    if (sleep_s == 0)
        sleep_s = 24u * 3600u;                  /* "leak only": still check in daily */
    hbn_sleep(sleep_s);
}
