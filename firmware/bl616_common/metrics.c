#include "metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "wifi_check.h"

#define MAGIC 0x4d455431u                    /* "MET1": the record is set */
#define K230_VER_CAP 40                      /* "0.6+g4ec6b02-dirty-202611061530" and room */

/* at a fixed place in both firmwares: .hbn_noinit sections are sorted by name (bl616_leakcam.ld.in); lost only with
 * the battery */
static struct {
    uint32_t magic;
    uint32_t boots;                          /* BL616 boots since the battery was connected */
    uint32_t fails;                          /* failed check-ins since the last good one */
    uint32_t switches, usb_wakes;            /* since the last good check-in */
    uint32_t k_sessions, k_failed, k_on_ms;  /* K230 sessions since the last good check-in, failed ones, rails-on ms */
    uint32_t k_last_ms, k_last_boot;         /* the last session: its length, the BL616 boot it ended in */
    int32_t k_last_ok, k_last_at_s;          /* ... ok 1 / failed 0 / none -1; BL616 uptime s at its end */
    uint32_t k_up_s, k_heap, k_heap_min, k_pages, k_boot;   /* the last STATUS; k_boot: the BL616 boot it came in */
    int32_t k_tdie_x10, k_at_s;
    char k_ver[K230_VER_CAP];
} rec __attribute__((section(".hbn_noinit.metrics")));

static uint8_t rst;                          /* GLB reset recorder bits of this boot (bl616_glb.h) */
static int rh_x10 = -1, t_x10, probe_mv = -1, tdie_x10 = -1000, vdd_mv = -1;

static uint32_t up_s(void)
{
    return (uint32_t)(bflb_mtimer_get_time_ms() / 1000);
}

void metrics_boot(void)
{
    if (rec.magic != MAGIC) {
        memset(&rec, 0, sizeof(rec));
        rec.k_last_ok = -1;
        rec.magic = MAGIC;
    }
    rec.boots++;
    GLB_RESET_RECORD_Type r;
    if (GLB_Get_Reset_Reason(&r) == SUCCESS)
        memcpy(&rst, &r, 1);
    GLB_Clr_Reset_Reason();                  /* the next boot reads only its own cause */
}

void metrics_switch(void)
{
    rec.switches++;
}

void metrics_usb_wake(void)
{
    rec.usb_wakes++;
}

/* STATUS's <uptime s>,<version>,<die x10>,<heap free>,<heap min>,<pages free B> (k230_link.h) */
void metrics_k230_status(const char *args)
{
    char *q;
    rec.k_up_s = (uint32_t)strtoul(args, &q, 10);
    rec.k_at_s = (int32_t)up_s();
    rec.k_boot = rec.boots;
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

void metrics_k230_session(uint32_t on_ms, bool ok)
{
    rec.k_sessions++;
    rec.k_failed += !ok;
    rec.k_on_ms += on_ms;
    rec.k_last_ms = on_ms;
    rec.k_last_ok = ok;
    rec.k_last_at_s = (int32_t)up_s();
    rec.k_last_boot = rec.boots;
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

void metrics_sample(void)
{
    /* the setup firmware gates every clock it does not need after each PDS wake (setup_sleep.c clocks_after_wake()):
     * I2C0 and the ADC on again here; the next wake gates them again */
    PERIPHERAL_CLOCK_I2C0_ENABLE();
    PERIPHERAL_CLOCK_ADC_DAC_ENABLE();
    aht20_init();
    bool started = aht20_start() == AHT20_OK;   /* the AHT20 converts (~37 ms) while the ADC reads */
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    struct bflb_device_s *adc = bflb_device_get_by_name("adc");
    bflb_gpio_init(gpio, PIN_LEAK_SENS, GPIO_ANALOG | GPIO_SMT_EN | GPIO_DRV_0);
    probe_mv = adc_mv(adc, ADC_CHANNEL_0, false);
    vdd_mv = adc_mv(adc, ADC_CHANNEL_VABT_HALF, true);
    tdie_x10 = die_x10(adc);
    if (!started || aht20_finish(&rh_x10, &t_x10) != AHT20_OK) {
        rh_x10 = -1;                         /* as WAKE's fields: rh -1 = no reading, t then 0 */
        t_x10 = 0;
    }
    aht20_deinit();
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
    uint32_t now = up_s();
    mem_stat(&m);
    ADD("&bat=%d&chg=%d&t=%d&rh=%d&probe=%d&tdie=%d&vdd=%d", battery_mv(), battery_usb_power() ? 1 : 0, t_x10, rh_x10,
        probe_mv, tdie_x10, vdd_mv);
    ADD("&up=%lu&boots=%lu&rst=%02x&crash=%lu", (unsigned long)now, (unsigned long)rec.boots, rst,
        crash_rec_get(&c) ? (unsigned long)c.count : 0UL);
    ADD("&free=%u&minfree=%u&blocks=%lu", (unsigned)m.free_bytes, (unsigned)m.min_free, (unsigned long)m.blocks);
    ADD("&rssi=%d&ch=%u&scan=%lu&join=%lu&dhcp=%lu&fails=%lu", w->rssi, w->channel, (unsigned long)w->scan_ms,
        (unsigned long)w->join_ms, (unsigned long)w->dhcp_ms, (unsigned long)rec.fails);
    ADD("&sw=%lu&usbw=%lu&ks=%lu&kfail=%lu&kon=%lu", (unsigned long)rec.switches, (unsigned long)rec.usb_wakes,
        (unsigned long)rec.k_sessions, (unsigned long)rec.k_failed, (unsigned long)rec.k_on_ms);
    /* ages in s: known only within this boot (no clock across a reset yet: the RTC comes with the sleep, DESIGN 5.3.10) */
    long kago = rec.k_last_ok >= 0 && rec.k_last_boot == rec.boots ? (long)(now - (uint32_t)rec.k_last_at_s) : -1;
    ADD("&klast=%ld&kms=%lu&kago=%ld", (long)rec.k_last_ok, (unsigned long)rec.k_last_ms, kago);
    if (rec.k_ver[0]) {
        char ver[3 * K230_VER_CAP + 1];
        size_t o = 0;
        for (const char *p = rec.k_ver; *p && o + 4 < sizeof(ver); p++)
            o += (size_t)snprintf(ver + o, sizeof(ver) - o, *p == '+' ? "%%2B" : "%c", *p);
        long sago = rec.k_boot == rec.boots ? (long)(now - (uint32_t)rec.k_at_s) : -1;
        ADD("&kver=%s&kup=%lu&ktdie=%ld&kheap=%lu&kheapmin=%lu&kpages=%lu&ksago=%ld", ver, (unsigned long)rec.k_up_s,
            (long)rec.k_tdie_x10, (unsigned long)rec.k_heap, (unsigned long)rec.k_heap_min, (unsigned long)rec.k_pages,
            sago);
    }
    return len;
}

void metrics_checkin(bool ok)
{
    if (!ok) {
        rec.fails++;
        return;
    }
    rec.fails = rec.switches = rec.usb_wakes = 0;
    rec.k_sessions = rec.k_failed = rec.k_on_ms = 0;
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
