#include "usb_power.h"

#include "bflb_acomp.h"
#include "bflb_gpio.h"
#include "bflb_mtimer.h"
#include "bl616_common.h"   /* ENABLE */
#include "board_pins.h"

void usb_sense_init(void)
{
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    bflb_gpio_init(gpio, PIN_USB_PGOOD, GPIO_ANALOG | GPIO_FLOAT | GPIO_DRV_0);
    /* the reference is vio_sel x 50 mV; the scaling factor has no effect (test034): 33 = 1.65 V, half of 3V3_SLEEP */
    struct bflb_acomp_config_s cfg = {
        .mux_en = ENABLE,
        .pos_chan_sel = AON_ACOMP_CHAN_ADC3,                    /* IO03 */
        .neg_chan_sel = AON_ACOMP_CHAN_VIO_X_SCALING_FACTOR_1,
        .vio_sel = 33,
        .scaling_factor = AON_ACOMP_SCALING_FACTOR_1,
        .bias_prog = AON_ACOMP_BIAS_POWER_MODE1,
        .hysteresis_pos_volt = AON_ACOMP_HYSTERESIS_VOLT_50MV,
        .hysteresis_neg_volt = AON_ACOMP_HYSTERESIS_VOLT_50MV,
    };
    bflb_acomp_init(AON_ACOMP0_ID, &cfg);
    bflb_acomp_enable(AON_ACOMP0_ID);
    bflb_mtimer_delay_ms(1);
}

bool usb_present(void)
{
    return bflb_acomp_get_result(AON_ACOMP0_ID) == 0;
}
