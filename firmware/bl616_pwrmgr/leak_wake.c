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
#include "board_pins.h"

#include "bflb_gpio.h"
#include "bflb_acomp.h"
#include "bflb_mtimer.h"
#include "bl616_hbn.h"
#include "bl616_pm.h"
#include "log.h"

#define LEAK_ACOMP          AON_ACOMP1_ID
#define ACOMP_VIO_1V65      33          /* vio_sel is in 50 mV steps; SDK: DEFAULT_ACOMP_VREF_1V65 */
#define PERSIST_MAGIC       0x4C430000u /* 'LC' in the top half, flags in the bottom half */

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

enum wake_reason wake_reason_get(void)
{
    /* The HBN interrupt state survives the wake-up reboot until it is cleared. To be confirmed
     * on hardware: if the BootROM clears it, read it from HBN_Get_Reset_Event() instead. */
    enum wake_reason r = WAKE_COLD;
    if (HBN_Get_INT_State(HBN_INT_ACOMP1) == SET)
        r = WAKE_LEAK;
    else if (HBN_Get_INT_State(HBN_INT_RTC) == SET)
        r = WAKE_RTC;
    HBN_Clear_IRQ(HBN_INT_ACOMP1);
    HBN_Clear_IRQ(HBN_INT_RTC);
    return r;
}

const char *wake_reason_name(enum wake_reason r)
{
    switch (r) {
        case WAKE_LEAK: return "leak";
        case WAKE_RTC:  return "rtc";
        default:        return "cold";
    }
}

uint32_t persist_get(void)
{
    uint32_t v = HBN_Get_Status_Flag();
    return ((v & 0xFFFF0000u) == PERSIST_MAGIC) ? (v & 0xFFFFu) : 0;
}

void persist_set(uint32_t v)
{
    HBN_Set_Status_Flag(PERSIST_MAGIC | (v & 0xFFFFu));
}

void hbn_sleep(uint32_t seconds)
{
    /* The comparator only wakes on an edge. If the probes are already wet there will be no
     * falling edge, so wake on the rising (dry again) edge instead and let the RTC re-report. */
    HBN_Clear_IRQ(HBN_INT_ACOMP1);
    HBN_Enable_AComp_IRQ(LEAK_ACOMP, leak_is_wet() ? HBN_ACOMP_INT_EDGE_POSEDGE : HBN_ACOMP_INT_EDGE_NEGEDGE);

    LOG_I("hbn: sleeping %u s, probes %s\r\n", (unsigned)seconds, leak_is_wet() ? "wet" : "dry");
    bflb_mtimer_delay_ms(5);                               /* let the USB console drain */
    /* RTC ticks at 32768 Hz (Y3 crystal on IO16/IO17, RC32K if it is not started) */
    pm_hbn_mode_enter(PM_HBN_LEVEL_0, (uint64_t)seconds * 32768u);
    while (1) {
    }
}
