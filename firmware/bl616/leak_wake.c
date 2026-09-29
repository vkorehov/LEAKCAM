/*
 * Leak detection and hibernate on the BL616.
 *
 * LEAK_SENS (GPIO20) is R71 1 M from 3V3_SLEEP / R72 1 M to GND, with the probes S1..S3
 * across R72. Dry it sits at 1.65 V, which is between the BL616's VIL (0.3 VDD = 0.99 V) and
 * VIH (0.7 VDD = 2.31 V): as a digital input it is undefined and would draw shoot-through
 * current. So the pad is kept in analog mode and watched by the always-on comparator ACOMP1,
 * whose positive input is ADC channel 0 = GPIO20 (same setup as the SDK's bl616dk board).
 *
 * Threshold 0.825 V = VIO 1.65 V x 0.5. Water resistance at the trip point:
 *   3.3 * (1M || Rw) / (1M + 1M || Rw) = 0.825  ->  Rw = 500 k  (406 k with 2 x 47 k in series)
 */
#include "leak_wake.h"
#include "aon_state.h"
#include "board_pins.h"
#include "evlog.h"

#include "bflb_adc.h"
#include "bflb_gpio.h"
#include "bflb_acomp.h"
#include "bflb_mtimer.h"
#include "bl616_hbn.h"
#include "bl616_pm.h"
#include "log.h"

#define LEAK_ACOMP          AON_ACOMP1_ID
#define ACOMP_VIO_1V65      33          /* vio_sel is in 50 mV steps; SDK: DEFAULT_ACOMP_VREF_1V65 */

void leak_init(void)
{
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    bflb_gpio_init(gpio, PIN_LEAK_SENS, GPIO_ANALOG | GPIO_FLOAT | GPIO_DRV_0);

    struct bflb_acomp_config_s cfg = {
        .mux_en = ENABLE,
        .pos_chan_sel = AON_ACOMP_CHAN_ADC0,                    /* GPIO20 */
        .neg_chan_sel = AON_ACOMP_CHAN_VIO_X_SCALING_FACTOR_1,
        .vio_sel = ACOMP_VIO_1V65,
        .scaling_factor = AON_ACOMP_SCALING_FACTOR_0P5,         /* -> 0.825 V */
        .bias_prog = AON_ACOMP_BIAS_POWER_MODE1,                /* slowest, lowest current */
        .hysteresis_pos_volt = AON_ACOMP_HYSTERESIS_VOLT_30MV,  /* probes in a film of water chatter */
        .hysteresis_neg_volt = AON_ACOMP_HYSTERESIS_VOLT_30MV,
    };
    bflb_acomp_init(LEAK_ACOMP, &cfg);
    bflb_acomp_enable(LEAK_ACOMP);
    bflb_mtimer_delay_ms(1);                                     /* comparator settling, slow bias */
}

bool leak_is_wet(void)
{
    /* output is 1 while the positive input (probe node) is above the reference */
    return bflb_acomp_get_result(LEAK_ACOMP) == 0;
}

/* The probe node in mV, by the GPADC on the same pad (ADC channel 0). C109 (100 nF) holds the
 * node, so the sample capacitor charges from it rather than through the 500 k divider (1 M || 1 M,
 * less when wet): one conversion is the reading. The SDK only runs the ADC continuously and its
 * adc_poll_onechan example drops the first results after the start, so the sixth is taken.
 * Absolute accuracy is to be measured on the board; the comparator stays the wake source, this
 * value is data. */
#define ADC_SETTLE_RESULTS 5

int leak_probe_mv(void)
{
    struct bflb_device_s *adc = bflb_device_get_by_name("adc");
    struct bflb_adc_config_s cfg = {
        .clk_div = ADC_CLK_DIV_32,
        .scan_conv_mode = false,
        .continuous_conv_mode = true,         /* the SDK does not support single mode */
        .differential_mode = false,
        .resolution = ADC_RESOLUTION_16B,
        .vref = ADC_VREF_3P2V,
    };
    struct bflb_adc_channel_s chan = { .pos_chan = ADC_CHANNEL_0, .neg_chan = ADC_CHANNEL_GND };
    uint32_t raw = 0;
    bool got = false;

    bflb_adc_init(adc, &cfg);
    bflb_adc_channel_config(adc, &chan, 1);
    bflb_adc_start_conversion(adc);
    for (int i = 0, waited = 0; !got && waited < 50; ) {
        if (bflb_adc_get_count(adc) == 0) {
            bflb_mtimer_delay_ms(1);
            waited++;
            continue;
        }
        raw = bflb_adc_read_raw(adc);
        got = i++ == ADC_SETTLE_RESULTS;
    }
    bflb_adc_stop_conversion(adc);
    int mv = -1;
    if (got) {
        struct bflb_adc_result_s r;
        bflb_adc_parse_result(adc, &raw, &r, 1);
        mv = r.millivolt;
    }
    bflb_adc_deinit(adc);
    return mv;
}

enum wake_reason wake_reason_get(void)
{
    /* The HBN interrupt state survives the wake-up reboot until it is cleared. To be confirmed
     * on hardware: if the BootROM clears it, read it from HBN_Get_Reset_Event() instead. */
    enum wake_reason r = WAKE_COLD;
    if (HBN_Get_INT_State(HBN_INT_ACOMP1) == SET)
        r = WAKE_LEAK;
    else if (HBN_Get_INT_State(HBN_INT_ACOMP0) == SET)
        r = WAKE_USB;
    else if (HBN_Get_INT_State(HBN_INT_RTC) == SET)
        r = WAKE_RTC;
    HBN_Clear_IRQ(HBN_INT_ACOMP1);
    HBN_Clear_IRQ(HBN_INT_ACOMP0);
    HBN_Clear_IRQ(HBN_INT_RTC);
    return r;
}

const char *wake_reason_name(enum wake_reason r)
{
    switch (r) {
        case WAKE_LEAK: return "leak";
        case WAKE_RTC:  return "rtc";
        case WAKE_USB:  return "usb";
        case WAKE_HUMID: return "humid";
        default:        return "cold";
    }
}

void rtc_use_crystal(void)
{
    /* Idempotent: after an HBN wake the crystal is still running. On a cold boot it needs up to
     * ~1 s to start; until then the RTC runs from RC32K (a few % off), fine for minute-scale wakes.
     * To verify on hardware: frequency on IO17 and RTC drift over a day. */
    HBN_Power_On_Xtal_32K();
    HBN_32K_Sel(HBN_32K_XTAL);
}

void hbn_sleep(uint32_t seconds)
{
    /* The comparator only wakes on an edge. If the probes are already wet there will be no
     * falling edge, so wake on the rising (dry again) edge instead and let the RTC re-report. */
    HBN_Clear_IRQ(HBN_INT_ACOMP1);
    HBN_Enable_AComp_IRQ(LEAK_ACOMP, leak_is_wet() ? HBN_ACOMP_INT_EDGE_POSEDGE : HBN_ACOMP_INT_EDGE_NEGEDGE);

    aon_prepare_sleep();                                   /* persisted state + clock in HBN RAM */
    LOG_I("hbn: sleeping %u s, probes %s\r\n", (unsigned)seconds, leak_is_wet() ? "wet" : "dry");
    evlog_add(EV_SLEEP, leak_is_wet(), seconds);
    bflb_mtimer_delay_ms(5);                               /* let the USB console drain */
    /* RTC ticks at 32768 Hz (Y3 crystal on IO16/IO17 via rtc_use_crystal(), else RC32K) */
    pm_hbn_mode_enter(PM_HBN_LEVEL_0, (uint64_t)seconds * 32768u);
    while (1) {
    }
}
