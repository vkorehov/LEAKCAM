/*
 * LEAKCAM BL616 power manager, proof of concept.
 *
 * The BL616 is the always-on part (3V3_SLEEP). It owns the K230's power: it wakes on a leak
 * (ACOMP1 on GPIO20) or on its RTC, powers the K230 up in the order the K230 guide requires,
 * lets it take pictures, and cuts it again when the K230 says it is done or stops responding.
 * Between sessions it hibernates (HBN level 0), which also forces the K230 off: HBN releases
 * every non-AON pad and R19 pulls K230_PWR low.
 *
 * On USB power the BL616 does not hibernate at all: it starts FreeRTOS and BLE, advertises for
 * pairing (ble_pairing.c) and keeps watching the probes, until USB goes away. The charger's power
 * path then feeds the system from USB, so staying awake costs the battery nothing.
 *
 * Production note: the BL616 is also the K230's Wi-Fi (SDIO, examples/wifi/sdio_wifi +
 * nethub host_linux). There is only one BL616 firmware, so this logic has to move into that
 * application as a task; it is kept standalone here to exercise the power paths in isolation.
 */
#include <stdio.h>
#include <string.h>

#include <FreeRTOS.h>
#include "task.h"

#include "board.h"
#include "bflb_mtimer.h"
#include "bflb_mtd.h"
#include "bl616_glb.h"
#include "easyflash.h"
#include "rfparam_adapter.h"
#include "log.h"

#include "aht20.h"
#include "aon_state.h"
#include "ble_pairing.h"
#include "k230_power.h"
#include "k230_link.h"
#include "leak_wake.h"
#include "usb_power.h"

#define BOOT_TIMEOUT_MS      90000   /* SPI NAND boot + Linux + agent start; measure and tighten */
#define HEARTBEAT_TIMEOUT_MS 10000   /* agent toggles K230 GPIO2 every 500 ms */
#define SESSION_MAX_MS       300000
#define HALT_TIMEOUT_MS      20000
#define HALT_GRACE_MS        500     /* after HALTED: let the last NAND program finish */
#define BOOT_RETRIES         3

#define REPORT_PERIOD_S      (6u * 3600u)
#define HUM_PERIOD_S         600u    /* humidity sample interval: RTC wake, ~85 ms awake, no K230 */
#define HUM_ALARM_X10        850     /* 85.0 %RH starts a K230 session ... */
#define HUM_CLEAR_X10        750     /* ... re-armed once it falls below 75.0 %RH */
#define RETRY_PERIOD_S       300u
#define GIVE_UP_PERIOD_S     3600u

/* persisted across hibernate in HBN RAM (aon_state.c, 8 flag bits) */
#define PF_FAILS_MASK        0x0Fu
#define PF_LEAK_REPORTED     0x10u
#define PF_FROM_USB_MODE     0x20u   /* reboot was the USB-unplug exit, not a real cold start */
#define PF_HUMID_REPORTED    0x40u

/* last humidity sample of this boot, sent to the K230 as ENV,<rh_x10>,<t_x10>; rh < 0 = none */
static int env_rh_x10 = -1, env_t_x10;

#define USB_UNPLUG_DEBOUNCE_MS 1000

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

static void send_wake(enum wake_reason reason)
{
    link_send("WAKE", wake_reason_name(reason));
    if (env_rh_x10 >= 0) {
        char env[16];
        snprintf(env, sizeof(env), "%d,%d", env_rh_x10, env_t_x10);
        link_send("ENV", env);
    }
    /* the K230 has no clock of its own after power-up; ours runs on the Y3 crystal */
    uint32_t now;
    if (wallclock_get(&now)) {
        char t[12];
        snprintf(t, sizeof(t), "%lu", (unsigned long)now);
        link_send("TIME", t);
    }
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
    send_wake(reason);

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
                send_wake(reason);
            /* the K230 learned real time (NTP over Wi-Fi): take it, correcting crystal drift */
            if (strcmp(m.cmd, "TIME") == 0 && m.has_arg)
                link_send("ACK", wallclock_set(m.arg) ? "TIME" : "TIME,refused");
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
                    send_wake(reason);
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

/* ---------------- battery: schedule the next hibernate ---------------- */

static void sleep_for(uint32_t pf, uint32_t seconds)
{
    if (seconds == 0)
        seconds = 24u * 3600u;                  /* "leak only": still check in daily */
    /* the K230's requested interval is served in HUM_PERIOD_S slices: every slice is an RTC wake
     * that samples humidity; the count left is carried across the reboots */
    uint32_t slices = (seconds + HUM_PERIOD_S - 1) / HUM_PERIOD_S;
    uint32_t first = seconds < HUM_PERIOD_S ? seconds : HUM_PERIOD_S;
    aht20_deinit();
    usb_arm_hbn_wake();                         /* a USB plug wakes us through ACOMP0 */
    persist_set(pf, slices - 1);
    hbn_sleep(first);
}

/* ---------------- USB powered: stay awake, BLE pairing ---------------- */

static volatile bool session_running;
static volatile bool usb_leak_reported;

static void session_task(void *arg)
{
    (void)arg;
    uint32_t sleep_s;
    run_session(WAKE_LEAK, &sleep_s);           /* K230 captures, then asks to be powered off */
    session_running = false;
    vTaskDelete(NULL);
}

static void ble_task(void *arg)
{
    (void)arg;
    ble_pairing_start();
    vTaskDelete(NULL);
}

static void supervisor_task(void *arg)
{
    uint32_t pf = (uint32_t)(uintptr_t)arg;
    uint64_t gone_since = 0;
    while (1) {
        uint64_t now = bflb_mtimer_get_time_ms();

        /* leak while on USB: same response as on battery, once per wet episode */
        bool wet = leak_is_wet();
        if (wet && !usb_leak_reported && !session_running) {
            usb_leak_reported = true;
            session_running = true;
            xTaskCreate(session_task, "k230", 2048, NULL, 2, NULL);
        }
        if (!wet)
            usb_leak_reported = false;

        /* unplug: leave through a clean reboot into the battery path */
        if (usb_present()) {
            gone_since = 0;
        } else if (gone_since == 0) {
            gone_since = now;
        } else if (now - gone_since > USB_UNPLUG_DEBOUNCE_MS && !session_running) {
            LOG_I("usb: unplugged, back to battery mode\r\n");
            ble_pairing_stop();
            vTaskDelay(pdMS_TO_TICKS(200));     /* let the disconnect go out */
            persist_set(pf | PF_FROM_USB_MODE | (usb_leak_reported ? PF_LEAK_REPORTED : 0), 0);
            GLB_SW_System_Reset();
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void usb_mode(uint32_t pf)
{
    LOG_I("usb: powered, staying awake for BLE pairing\r\n");
    bflb_mtd_init();
    easyflash_init();                           /* BLE bonds and Wi-Fi credentials */
    if (rfparam_init(0, NULL, 0) != 0) {
        LOG_E("usb: RF init failed, BLE unavailable\r\n");
    } else {
        xTaskCreate(ble_task, "ble", 1024, NULL, configMAX_PRIORITIES - 2, NULL);
    }
    xTaskCreate(supervisor_task, "usb", 1024, (void *)(uintptr_t)pf, 3, NULL);
    vTaskStartScheduler();
    while (1) {
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
    usb_sense_init();
    rtc_use_crystal();
    aon_init();                                 /* HBN RAM: flags and wall clock, before any use */
    uint32_t pf, hum_left;
    persist_get(&pf, &hum_left);
    bool wet = leak_is_wet();
    LOG_I("boot: wake=%s usb=%d probes=%s fails=%u\r\n", wake_reason_name(reason), usb_present(),
          wet ? "wet" : "dry", (unsigned)(pf & PF_FAILS_MASK));

    if (usb_present())
        usb_mode(pf & ~PF_FROM_USB_MODE);       /* does not return */

    /* humidity is sampled on every battery boot; the AHT20 cannot wake us by itself */
    aht20_init();
    int rh, t;
    enum aht20_result hr = aht20_read(&rh, &t);
    if (hr == AHT20_OK) {
        env_rh_x10 = rh;
        env_t_x10 = t;
        LOG_I("aht20: %d.%d %%RH %d.%d C\r\n", rh / 10, rh % 10, t / 10, t < 0 ? -t % 10 : t % 10);
        if (rh < HUM_CLEAR_X10)
            pf &= ~PF_HUMID_REPORTED;
    } else {
        LOG_E("aht20: read failed (%d)\r\n", (int)hr);
    }

    /* a leak that is still present after an RTC wake is reported once, not every period */
    if (reason == WAKE_LEAK && !wet)
        reason = WAKE_COLD;                     /* edge came from drying out */
    if (reason == WAKE_RTC && wet && !(pf & PF_LEAK_REPORTED))
        reason = WAKE_LEAK;
    if ((reason == WAKE_RTC || reason == WAKE_COLD) && hr == AHT20_OK &&
        rh >= HUM_ALARM_X10 && !(pf & PF_HUMID_REPORTED))
        reason = WAKE_HUMID;

    /* humidity slice with nothing to report: straight back to sleep, the K230 stays off */
    if (reason == WAKE_RTC && hum_left > 0) {
        aht20_deinit();
        usb_arm_hbn_wake();
        persist_set(pf, hum_left - 1);
        hbn_sleep(HUM_PERIOD_S);
    }

    /* the reboot out of USB mode is not a new installation: no K230 session for it */
    if (pf & PF_FROM_USB_MODE) {
        pf &= ~PF_FROM_USB_MODE;
        if (reason != WAKE_LEAK)
            sleep_for(pf, REPORT_PERIOD_S);
    }

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
        if (reason == WAKE_HUMID)
            pf |= PF_HUMID_REPORTED;
        if (!wet)
            pf &= ~PF_LEAK_REPORTED;
    } else {
        fails = fails < PF_FAILS_MASK ? fails + 1 : fails;
        sleep_s = fails >= 3 ? GIVE_UP_PERIOD_S : RETRY_PERIOD_S;
    }
    sleep_for((pf & ~PF_FAILS_MASK) | fails, sleep_s);
}
