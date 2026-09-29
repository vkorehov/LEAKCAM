/*
 * LEAKCAM BL616 firmware, the only one: power manager, Wi-Fi for the K230, BLE provisioning.
 *
 * The BL616 is the always-on part (3V3_SLEEP). It owns the K230's power: it wakes on a leak
 * (ACOMP1 on GPIO20) or on its RTC, powers the K230 up in the order the K230 guide requires,
 * lets it take pictures, and cuts it again when the K230 says it is done or stops responding.
 * While the K230 is up and has asked for it (WIFI), the BL616 is its Wi-Fi (wifi_link.c, NetHub
 * over SDIO); nothing else starts Wi-Fi, and BLE runs only on USB power. Between sessions
 * it hibernates (HBN level 0), which also forces the K230 off: HBN releases every non-AON pad and
 * R19 pulls K230_PWR low.
 *
 * A battery wake decides bare-metal, without the scheduler: a humidity slice with nothing to
 * report goes back to sleep at once. Only a K230 session starts FreeRTOS (battery_task), because
 * the Wi-Fi stack runs in its own tasks.
 *
 * On USB power the BL616 does not hibernate at all: it starts FreeRTOS and BLE, advertises for
 * pairing (ble_pairing.c) and keeps watching the probes, until USB goes away. The charger's power
 * path then feeds the system from USB, so staying awake costs the battery nothing.
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
#include "wifi_link.h"

#define BOOT_TIMEOUT_MS      90000   /* SPI NAND boot + RT-Smart + agent start; measure and tighten */
/* The K230 keeps itself on for as long as its agent toggles GPIO2 (every 500 ms): once the edges
 * stop for this long the session is over and the power is cut. The agent stops them only when
 * it is done and its files are written. */
#define HEARTBEAT_STOP_MS    2000
#define BOOT_RETRIES         3

#define REPORT_PERIOD_S      (6u * 3600u)
#define HUM_PERIOD_S         600u    /* humidity sample interval: RTC wake, ~85 ms awake, no K230 */
#define HUM_ALARM_X10        850     /* 85.0 %RH and above wakes the K230, which decides */
#define RETRY_PERIOD_S       300u
#define GIVE_UP_PERIOD_S     3600u

/* persisted across hibernate in HBN RAM (aon_state.c, 8 flag bits) */
#define PF_FAILS_MASK        0x0Fu
#define PF_FROM_USB_MODE     0x20u   /* reboot was the USB-unplug exit, not a real cold start */

/* last humidity sample of this boot, sent to the K230 in WAKE; rh < 0 = none */
static int env_rh_x10 = -1, env_t_x10;

#define USB_UNPLUG_DEBOUNCE_MS 1000

enum session_end {
    END_DONE,            /* READY came, then the heartbeat stopped */
    END_NO_BOOT,         /* READY never arrived */
};

/* easyflash (BLE bonds, Wi-Fi credentials) and RF calibration: each once, shared by BLE and
 * Wi-Fi, and only when one of them is wanted: a battery session without WIFI powers neither */
static bool storage_up, radio_up;
static bool wifi_on;

static void storage_init(void)
{
    if (storage_up)
        return;
    bflb_mtd_init();
    easyflash_init();
    storage_up = true;
}

static void radio_init(void)
{
    if (radio_up)
        return;
    storage_init();
    if (rfparam_init(0, NULL, 0) != 0)
        LOG_E("radio: RF init failed, no BLE and no Wi-Fi\r\n");
    else
        radio_up = true;
}

/* the SDIO pads hang on the K230's rail: the link goes down before the rail does */
static void k230_off(void)
{
    if (wifi_on) {
        wifi_link_stop();
        wifi_on = false;
    }
    k230_power_off();
}

/* (re)sends queued commands; logs the ones the K230 never answered */
static void link_tick(void)
{
    const struct link_result *r = link_service(bflb_mtimer_get_time_ms());
    if (r && r->end == LINK_TIMED_OUT)
        LOG_W("link: %s not answered\r\n", r->cmd);
}

/* answered here, so even while the loop waits for something else:
 *   TIME  the K230 learned real time (NTP over Wi-Fi), take it, correcting crystal drift
 *   WIFI  refused without stored credentials or radio; started by run_session() once accepted */
static uint8_t reply(const struct link_msg *m)
{
    if (strcmp(m->cmd, "TIME") == 0)
        return m->has_arg && wallclock_set(m->arg) ? 0 : LINK_ERR_REFUSED;
    if (strcmp(m->cmd, "WIFI") == 0) {
        storage_init();                         /* credentials first: without them no RF at all */
        if (!wifi_link_has_credentials())
            return LINK_ERR_NO_CREDENTIALS;
        radio_init();
        return radio_up ? 0 : LINK_ERR_RADIO;
    }
    return 0;
}

static bool wait_link_cmd(const char *cmd, uint32_t timeout_ms, struct link_msg *m)
{
    uint64_t t0 = bflb_mtimer_get_time_ms();
    while (bflb_mtimer_get_time_ms() - t0 < timeout_ms) {
        link_tick();
        if (link_poll(m) && strcmp(m->cmd, cmd) == 0)
            return true;
        vTaskDelay(pdMS_TO_TICKS(2));           /* sessions run as tasks: let Wi-Fi run */
    }
    return false;
}

static void send_wake(enum wake_reason reason)
{
    /* WAKE,<reason>,<unix s>,<rh_x10>,<t_x10>,<probe_mv>. The K230's RTC restarts at every
     * power-up, ours runs on the Y3 crystal, so the time rides in this frame (0 = our clock is
     * not valid, the battery was out); so do the sensors, which take part in the leak decision
     * whatever the cameras see (rh -1 = no AHT20 sample: USB mode or a failed read) */
    uint32_t now;
    char wake[48];
    if (!wallclock_get(&now))
        now = 0;
    snprintf(wake, sizeof(wake), "%s,%lu,%d,%d,%d", wake_reason_name(reason), (unsigned long)now,
             env_rh_x10, env_rh_x10 >= 0 ? env_t_x10 : 0, leak_probe_mv());
    link_send_cmd("WAKE", wake);
}

static enum session_end run_session(enum wake_reason reason, uint32_t *sleep_s)
{
    struct link_msg m;
    k230_power_on();

    link_reset();
    link_set_reply(reply);
    if (!wait_link_cmd("READY", BOOT_TIMEOUT_MS, &m)) {
        LOG_E("session: no READY within %u ms\r\n", BOOT_TIMEOUT_MS);
        k230_off();                             /* nothing to sync with, K230 never came up */
        return END_NO_BOOT;
    }
    send_wake(reason);

    /* the K230 decides how long it stays: as long as its heartbeat runs. SLEEP,<s> only says
     * when to wake it next; without one the default period applies */
    uint64_t last_edge = bflb_mtimer_get_time_ms();
    bool level = k230_alive_level();
    *sleep_s = REPORT_PERIOD_S;
    while (1) {
        uint64_t now = bflb_mtimer_get_time_ms();
        bool l = k230_alive_level();
        if (l != level) {
            level = l;
            last_edge = now;
        }
        if (now - last_edge > HEARTBEAT_STOP_MS) {
            LOG_I("session: heartbeat stopped, next wake in %u s\r\n", (unsigned)*sleep_s);
            k230_off();
            return END_DONE;
        }

        link_tick();
        if (link_poll(&m)) {                    /* already answered by the link layer */
            if (strcmp(m.cmd, "SLEEP") == 0)
                *sleep_s = m.has_arg ? m.arg : REPORT_PERIOD_S;
            if (strcmp(m.cmd, "READY") == 0)    /* agent restarted inside the session */
                send_wake(reason);
            if (strcmp(m.cmd, "WIFI") == 0 && !wifi_on) {   /* accepted by reply() */
                if (wifi_link_start() == 0)
                    wifi_on = true;
                else
                    LOG_E("session: Wi-Fi start failed\r\n");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
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
static volatile bool usb_was_wet;

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

        /* probes turning wet while on USB: a K230 session, as on battery. On the edge only: we
         * never sleep here, so a level would restart the session for as long as it stays wet */
        bool wet = leak_is_wet();
        if (wet && !usb_was_wet && !session_running) {
            session_running = true;
            xTaskCreate(session_task, "k230", 2048, NULL, 2, NULL);
        }
        usb_was_wet = wet;

        /* unplug: leave through a clean reboot into the battery path */
        if (usb_present()) {
            gone_since = 0;
        } else if (gone_since == 0) {
            gone_since = now;
        } else if (now - gone_since > USB_UNPLUG_DEBOUNCE_MS && !session_running) {
            LOG_I("usb: unplugged, back to battery mode\r\n");
            ble_pairing_stop();
            vTaskDelay(pdMS_TO_TICKS(200));     /* let the disconnect go out */
            persist_set(pf | PF_FROM_USB_MODE, 0);
            GLB_SW_System_Reset();
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void usb_mode(uint32_t pf)
{
    LOG_I("usb: powered, staying awake for BLE pairing\r\n");
    radio_init();
    if (radio_up)
        xTaskCreate(ble_task, "ble", 1024, NULL, configMAX_PRIORITIES - 2, NULL);
    xTaskCreate(supervisor_task, "usb", 1024, (void *)(uintptr_t)pf, 3, NULL);
    vTaskStartScheduler();
    while (1) {
    }
}

/* ---------------- battery: one K230 session, then hibernate ---------------- */

static enum wake_reason bat_reason;
static uint32_t bat_pf;

static void battery_task(void *arg)
{
    (void)arg;
    enum wake_reason reason = bat_reason;
    uint32_t pf = bat_pf;

    uint32_t sleep_s = REPORT_PERIOD_S;
    enum session_end end = END_NO_BOOT;
    unsigned fails = pf & PF_FAILS_MASK;

    for (unsigned attempt = 0; attempt < BOOT_RETRIES; attempt++) {
        end = run_session(reason, &sleep_s);
        if (end == END_DONE)
            break;
        LOG_W("session: attempt %u ended with %d, retrying\r\n", attempt + 1, (int)end);
    }

    if (end == END_DONE) {
        fails = 0;
    } else {
        fails = fails < PF_FAILS_MASK ? fails + 1 : fails;
        sleep_s = fails >= 3 ? GIVE_UP_PERIOD_S : RETRY_PERIOD_S;
    }
    sleep_for((pf & ~PF_FAILS_MASK) | fails, sleep_s);
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
    } else {
        LOG_E("aht20: read failed (%d)\r\n", (int)hr);
    }

    /* no memory of what was reported: a sensor over its threshold wakes the K230, every time, and
     * the K230 decides (it gets the values in WAKE). A drying edge is a leak wake too: the probe
     * voltage tells the K230 which way it went. */
    if (reason == WAKE_RTC && wet)
        reason = WAKE_LEAK;
    if ((reason == WAKE_RTC || reason == WAKE_COLD) && hr == AHT20_OK && rh >= HUM_ALARM_X10)
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

    bat_reason = reason;
    bat_pf = pf;
    xTaskCreate(battery_task, "k230", 2048, NULL, 2, NULL);
    vTaskStartScheduler();
    while (1) {
    }
}
