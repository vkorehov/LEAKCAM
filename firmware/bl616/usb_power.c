#include "usb_power.h"
#include "board_pins.h"

#include "bflb_gpio.h"
#include "bflb_acomp.h"
#include "bflb_mtimer.h"
#include "bl616_hbn.h"

#define USB_ACOMP        AON_ACOMP0_ID
#define ACOMP_VIO_1V65   33             /* 50 mV steps; PGOOD swings 0 V / 3.3 V, 1.65 V is mid */

void usb_sense_init(void)
{
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    bflb_gpio_init(gpio, PIN_USB_PGOOD, GPIO_ANALOG | GPIO_FLOAT | GPIO_DRV_0);
    struct bflb_acomp_config_s cfg = {
        .mux_en = ENABLE,
        .pos_chan_sel = AON_ACOMP_CHAN_ADC3,                    /* GPIO3 */
        .neg_chan_sel = AON_ACOMP_CHAN_VIO_X_SCALING_FACTOR_1,
        .vio_sel = ACOMP_VIO_1V65,
        .scaling_factor = AON_ACOMP_SCALING_FACTOR_1,           /* -> 1.65 V */
        .bias_prog = AON_ACOMP_BIAS_POWER_MODE1,
        .hysteresis_pos_volt = AON_ACOMP_HYSTERESIS_VOLT_50MV,
        .hysteresis_neg_volt = AON_ACOMP_HYSTERESIS_VOLT_50MV,
    };
    bflb_acomp_init(USB_ACOMP, &cfg);
    bflb_acomp_enable(USB_ACOMP);
    bflb_mtimer_delay_ms(1);
}

bool usb_present(void)
{
    return bflb_acomp_get_result(USB_ACOMP) == 0;    /* PGOOD below 1.65 V */
}

void usb_arm_hbn_wake(void)
{
    HBN_Clear_IRQ(HBN_INT_ACOMP0);
    HBN_Enable_AComp_IRQ(USB_ACOMP, HBN_ACOMP_INT_EDGE_NEGEDGE);
}
