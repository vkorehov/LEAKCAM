#include "metrics.h"
#include "thresholds.h"
#include "evlog.h"
#include "leakcam_version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "bflb_adc.h"
#include "bflb_clock.h"
#include "bflb_gpio.h"
#include "bflb_mtimer.h"
#include "bl616_glb.h"
#include "shell.h"

#include "aht20.h"
#include "battery.h"
#include "board_pins.h"
#include "crash_rec.h"
#include "mem_stat.h"
#include "rtc_clock.h"
#include "wifi_check.h"

#define MAGIC 0x4d455437u                    /* "MET7": the record is set (2: the leak check, 3: the intervals,
                                              * 4: the Wi-Fi change's result, 5: the room reading, 6: RTC ages and
                                              * HBN wakes, 7: the job's focus fields) */
#define JOB_CAP 32                           /* "unfocused,unfocused,999,999" (28; HBN RAM is full: 4 KB) */
#define K230_VER_CAP 40                      /* "0.6+g4ec6b02-dirty-202611061530" and room */

/* at a fixed place in both firmwares: .hbn_noinit sections are sorted by name (bl616_leakcam.ld.in); lost only with
 * the battery */
static struct {
    uint32_t magic;
    uint32_t boots;                          /* BL616 boots since the battery was connected */
    uint32_t fails;                          /* failed check-ins since the last good one */
    uint32_t switches, usb_wakes;            /* since the last good check-in */
    uint32_t hbn_wakes;                      /* ... HBN sleeps woken (the full firmware on battery: not boots) */
    uint32_t k_sessions, k_failed, k_on_ms;  /* K230 sessions since the last good check-in, failed ones, rails-on ms */
    uint32_t k_last_ms;                      /* the last session: its length */
    int32_t k_last_ok, k_last_at_s;          /* ... ok 1 / failed 0 / none -1; the RTC s at its end */
    uint32_t k_up_s, k_heap, k_heap_min, k_pages;   /* the last STATUS */
    int32_t k_tdie_x10, k_at_s;
    char k_ver[K230_VER_CAP];
    char job[JOB_CAP];                       /* the last leak check's DONE (k230_link.h), "" none */
    int32_t job_at_s;
    uint32_t checkin_s, leakcheck_s;         /* the server's intervals (DESIGN.md 5.3.12), 0 = none yet */
    int32_t wifi_change;                     /* the last Wi-Fi change from the server: its wifi_check state, 0 none
                                              * (DESIGN.md 5.3.13); until a good check-in reported it */
    int32_t room_t_x10, room_rh_x10;         /* the room, read after 3 h cool (DESIGN.md 5.3.15); rh -1 = none yet */
    int32_t room_at_s;
} rec __attribute__((section(".hbn_noinit.metrics")));

static uint8_t rst;                          /* GLB reset recorder bits of this boot (bl616_glb.h) */
static int rh_x10 = -1, t_x10, probe_mv = -1, tdie_x10 = -1000, vdd_mv = -1;
static int rh_sim = -1;                      /* `climate sim`: the humidity the AHT20 reads instead (tests, x10) */
static uint32_t (*quiet_fn)(void);           /* s the board has been cool now (climate.c), NULL: not tracked */
static SemaphoreHandle_t aht_mu;             /* one AHT20 read at a time: the climate timer and the check-in */
static StaticSemaphore_t aht_mu_buf;         /* static: metrics_boot() runs before board_init() sets up the heap (a
                                              * heap mutex there asserted before USB, 2026-11-10: BOOT + EN needed) */

static uint32_t up_s(void)
{
    return (uint32_t)(bflb_mtimer_get_time_ms() / 1000);
}

void metrics_boot(void)
{
    if (rec.magic != MAGIC) {
        memset(&rec, 0, sizeof(rec));
        rec.k_last_ok = -1;
        rec.room_rh_x10 = -1;
        rec.magic = MAGIC;
    }
    aht_mu = xSemaphoreCreateMutexStatic(&aht_mu_buf);
    rec.boots++;
    GLB_RESET_RECORD_Type r;
    if (GLB_Get_Reset_Reason(&r) == SUCCESS)
        memcpy(&rst, &r, 1);
    GLB_Clr_Reset_Reason();                  /* the next boot reads only its own cause */
    evlog_add(EV_BOOT, strcmp(LEAKCAM_IMAGE, "setup") == 0, rst);
}

void metrics_switch(void)
{
    rec.switches++;
}

void metrics_usb_wake(void)
{
    rec.usb_wakes++;
}

void metrics_hbn_wake(void)
{
    rec.boots--;                             /* metrics_boot() counted it: a wake from the sleep, not a boot */
    rec.hbn_wakes++;
}

/* STATUS's <uptime s>,<version>,<die x10>,<heap free>,<heap min>,<pages free B> (k230_link.h) */
void metrics_k230_status(const char *args)
{
    char *q;
    rec.k_up_s = (uint32_t)strtoul(args, &q, 10);
    rec.k_at_s = (int32_t)rtc_clock_s();
    if (*q != ',')
        return;
    const char *p = q + 1;
    const char *e = strchr(p, ',');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    if (n >= K230_VER_CAP)
        n = K230_VER_CAP - 1;
    memcpy(rec.k_ver, p, n);
    rec.k_ver[n] = 0;
    if (!e)
        return;
    rec.k_tdie_x10 = (int32_t)strtol(e + 1, &q, 10);
    rec.k_heap = *q == ',' ? (uint32_t)strtoul(q + 1, &q, 10) : 0;
    rec.k_heap_min = *q == ',' ? (uint32_t)strtoul(q + 1, &q, 10) : 0;
    rec.k_pages = *q == ',' ? (uint32_t)strtoul(q + 1, &q, 10) : 0;
}

void metrics_job(const char *result)
{
    snprintf(rec.job, sizeof(rec.job), "%s", result);
    rec.job_at_s = (int32_t)rtc_clock_s();
}

void metrics_intervals_set(uint32_t checkin_s, uint32_t leakcheck_s)
{
    if (checkin_s)
        rec.checkin_s = checkin_s;
    if (leakcheck_s)
        rec.leakcheck_s = leakcheck_s;
}

void metrics_intervals(uint32_t *checkin_s, uint32_t *leakcheck_s)
{
    *checkin_s = rec.checkin_s;
    *leakcheck_s = rec.leakcheck_s;
}

void metrics_k230_session(uint32_t on_ms, bool ok)
{
    rec.k_sessions++;
    rec.k_failed += !ok;
    rec.k_on_ms += on_ms;
    rec.k_last_ms = on_ms;
    rec.k_last_ok = ok;
    rec.k_last_at_s = (int32_t)rtc_clock_s();
}

/* one ADC conversion (the SDK's first results after a start are stale: the 6th, as tests/test021's leak_mv()) */
static bool adc_raw(struct bflb_device_s *adc, uint32_t *raw)
{
    bflb_adc_start_conversion(adc);
    bool got = false;
    for (int i = 0, waited = 0; !got && waited < 5000;) {
        if (bflb_adc_get_count(adc) == 0) {
            bflb_mtimer_delay_us(10);
            waited++;
            continue;
        }
        *raw = bflb_adc_read_raw(adc);
        got = i++ == 5;
    }
    bflb_adc_stop_conversion(adc);
    return got;
}

static int adc_mv(struct bflb_device_s *adc, int chan, bool vbat)
{
    struct bflb_adc_config_s cfg = {
        .clk_div = ADC_CLK_DIV_32, .scan_conv_mode = false, .continuous_conv_mode = true,
        .differential_mode = false, .resolution = ADC_RESOLUTION_16B, .vref = ADC_VREF_3P2V,
    };
    struct bflb_adc_channel_s ch = { .pos_chan = chan, .neg_chan = ADC_CHANNEL_GND };
    bflb_adc_init(adc, &cfg);                /* calibrates once per boot (28.6 ms, tests/test021) */
    bflb_adc_channel_config(adc, &ch, 1);
    if (vbat)
        bflb_adc_vbat_enable(adc);
    uint32_t raw;
    int mv = -1;
    if (adc_raw(adc, &raw)) {
        struct bflb_adc_result_s r;
        bflb_adc_parse_result(adc, &raw, &r, 1);
        mv = vbat ? r.millivolt * 2 : r.millivolt;   /* the VBAT channel is the supply halved */
    }
    if (vbat)
        bflb_adc_vbat_disable(adc);
    bflb_adc_deinit(adc);
    return mv;
}

/* the SDK's adc_tsen example: 2.0 V reference, the internal diode */
static int die_x10(struct bflb_device_s *adc)
{
    struct bflb_adc_config_s cfg = {
        .clk_div = ADC_CLK_DIV_32, .scan_conv_mode = false, .continuous_conv_mode = true,
        .differential_mode = false, .resolution = ADC_RESOLUTION_16B, .vref = ADC_VREF_2P0V,
    };
    struct bflb_adc_channel_s ch = { .pos_chan = ADC_CHANNEL_TSEN_P, .neg_chan = ADC_CHANNEL_GND };
    bflb_adc_init(adc, &cfg);
    bflb_adc_channel_config(adc, &ch, 1);
    bflb_adc_tsen_init(adc, ADC_TSEN_MOD_INTERNAL_DIODE);
    float t = bflb_adc_tsen_get_temp(adc);
    bflb_adc_stop_conversion(adc);
    bflb_adc_deinit(adc);
    return t < -60 || t > 200 ? -1000 : (int)(t * 10);   /* -ETIMEDOUT comes back as a temperature */
}

/* the AHT20 started (~37 ms to convert) before `meanwhile` runs, then read; rh -1 / t 0 on a failure; `climate sim`
 * replaces the humidity */
static bool aht20_read(int *rh, int *t, void (*meanwhile)(void))
{
    xSemaphoreTake(aht_mu, portMAX_DELAY);
    PERIPHERAL_CLOCK_I2C0_ENABLE();
    aht20_init();
    bool ok = aht20_start() == AHT20_OK;
    if (meanwhile)
        meanwhile();
    ok = ok && aht20_finish(rh, t) == AHT20_OK;
    aht20_deinit();
    if (!ok) {
        *rh = -1;                            /* as WAKE's fields: rh -1 = no reading, t then 0 */
        *t = 0;
    }
    if (rh_sim >= 0)
        *rh = rh_sim;
    rh_x10 = *rh;
    t_x10 = *t;
    xSemaphoreGive(aht_mu);
    return *rh >= 0;
}

bool metrics_aht20(int *rh, int *t)
{
    return aht20_read(rh, t, NULL);
}

void metrics_rh_sim(int x10)
{
    rh_sim = x10;
}

void metrics_quiet(uint32_t (*fn)(void))
{
    quiet_fn = fn;
}

void metrics_room(int t, int rh)
{
    rec.room_t_x10 = t;
    rec.room_rh_x10 = rh;
    rec.room_at_s = (int32_t)rtc_clock_s();
}

/* the probe node, the supply and the die, while the AHT20 converts */
static void adc_sample(void)
{
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    struct bflb_device_s *adc = bflb_device_get_by_name("adc");
    bflb_gpio_init(gpio, PIN_LEAK_SENS, GPIO_ANALOG | GPIO_SMT_EN | GPIO_DRV_0);
    probe_mv = adc_mv(adc, ADC_CHANNEL_0, false);
    vdd_mv = adc_mv(adc, ADC_CHANNEL_VABT_HALF, true);
    tdie_x10 = die_x10(adc);
}

void metrics_sample(void)
{
    /* the setup firmware gates every clock it does not need after each PDS wake (setup_sleep.c clocks_after_wake()):
     * I2C0 and the ADC on again here; the next wake gates them again */
    PERIPHERAL_CLOCK_ADC_DAC_ENABLE();
    int rh, t;
    aht20_read(&rh, &t, adc_sample);
}

#define ADD(...)                                                                     \
    do {                                                                             \
        int n_ = snprintf(form + len, cap - len, __VA_ARGS__);                       \
        if (n_ < 0 || (size_t)n_ >= cap - len)                                       \
            return cap;                                                              \
        len += (size_t)n_;                                                           \
    } while (0)

size_t metrics_form(char *form, size_t len, size_t cap)
{
    struct mem_stat m;
    struct crash_rec_info c;
    const struct wifi_check_link *w = wifi_check_last_link();
    uint32_t now = up_s(), at = rtc_clock_s();   /* ages on the RTC: through HBN sleep and resets */
    mem_stat(&m);
    ADD("&bat=%d&chg=%d&t=%d&rh=%d&probe=%d&tdie=%d&vdd=%d", battery_mv(), battery_usb_power() ? 1 : 0, t_x10, rh_x10,
        probe_mv, tdie_x10, vdd_mv);
    ADD("&up=%lu&boots=%lu&rst=%02x&crash=%lu", (unsigned long)now, (unsigned long)rec.boots, rst,
        crash_rec_get(&c) ? (unsigned long)c.count : 0UL);
    ADD("&free=%u&minfree=%u&blocks=%lu", (unsigned)m.free_bytes, (unsigned)m.min_free, (unsigned long)m.blocks);
    ADD("&rssi=%d&ch=%u&scan=%lu&join=%lu&dhcp=%lu&fails=%lu", w->rssi, w->channel, (unsigned long)w->scan_ms,
        (unsigned long)w->join_ms, (unsigned long)w->dhcp_ms, (unsigned long)rec.fails);
    ADD("&hbnw=%lu", (unsigned long)rec.hbn_wakes);
    ADD("&thmv=%d&thrh=%d", thresholds_probe_mv(), thresholds_humid());   /* the thresholds in use (thresholds.h) */
    ADD("&sw=%lu&usbw=%lu&ks=%lu&kfail=%lu&kon=%lu", (unsigned long)rec.switches, (unsigned long)rec.usb_wakes,
        (unsigned long)rec.k_sessions, (unsigned long)rec.k_failed, (unsigned long)rec.k_on_ms);
    /* ages in s: known only within this boot (no clock across a reset yet: the RTC comes with the sleep, DESIGN 5.3.10) */
    long kago = rec.k_last_ok >= 0 ? (long)(at - (uint32_t)rec.k_last_at_s) : -1;
    ADD("&klast=%ld&kms=%lu&kago=%ld", (long)rec.k_last_ok, (unsigned long)rec.k_last_ms, kago);
    if (rec.k_ver[0]) {
        char ver[3 * K230_VER_CAP + 1];
        size_t o = 0;
        for (const char *p = rec.k_ver; *p && o + 4 < sizeof(ver); p++)
            o += (size_t)snprintf(ver + o, sizeof(ver) - o, *p == '+' ? "%%2B" : "%c", *p);
        long sago = (long)(at - (uint32_t)rec.k_at_s);
        ADD("&kver=%s&kup=%lu&ktdie=%ld&kheap=%lu&kheapmin=%lu&kpages=%lu&ksago=%ld", ver, (unsigned long)rec.k_up_s,
            (long)rec.k_tdie_x10, (unsigned long)rec.k_heap, (unsigned long)rec.k_heap_min, (unsigned long)rec.k_pages,
            sago);
    }
    if (rec.job[0])
        ADD("&job=%s&jago=%ld", rec.job, (long)(at - (uint32_t)rec.job_at_s));
    if (rec.wifi_change)
        ADD("&wchg=%ld", (long)rec.wifi_change);
    if (rec.room_rh_x10 >= 0)
        ADD("&troom=%ld&rhroom=%ld&roomago=%ld", (long)rec.room_t_x10, (long)rec.room_rh_x10,
            (long)(at - (uint32_t)rec.room_at_s));
    if (quiet_fn)
        ADD("&quiet=%lu", (unsigned long)quiet_fn());
    return len;
}

void metrics_checkin(bool ok)
{
    if (!ok) {
        rec.fails++;
        return;
    }
    rec.fails = rec.switches = rec.usb_wakes = rec.hbn_wakes = 0;
    rec.k_sessions = rec.k_failed = rec.k_on_ms = 0;
    rec.wifi_change = 0;
}

void metrics_wifi_change(int state)
{
    rec.wifi_change = state;
}

static void cmd_metrics(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    static char form[640];
    metrics_sample();
    size_t n = metrics_form(form, 0, sizeof(form));
    printf("metrics: %s\r\n", n < sizeof(form) ? form : "over the form's room");
}
SHELL_CMD_EXPORT_ALIAS(cmd_metrics, metrics, the check-in metrics read now);
