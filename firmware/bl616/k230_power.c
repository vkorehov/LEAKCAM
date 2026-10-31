#include "k230_power.h"

#include "bflb_gpio.h"
#include "board_pins.h"

/* analog: no output, no input buffer (a half-powered pull-up cannot cause shoot-through) */
static void park(struct bflb_device_s *gpio, uint8_t pin)
{
    bflb_gpio_init(gpio, pin, GPIO_ANALOG | GPIO_FLOAT | GPIO_DRV_0);
}

void k230_power_init(void)
{
    /* the K230 link and SDIO pins, and the leak node (1.65 V dry sits between VIL and VIH: analog only, test034) */
    static const uint8_t parked[] = { PIN_LINK_TX, PIN_LINK_RX, PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1,
                                      PIN_SD_D2, PIN_SD_D3, PIN_LEAK_SENS };
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");

    bflb_gpio_init(gpio, PIN_K230_RSTN, GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_0);
    bflb_gpio_set(gpio, PIN_K230_RSTN);         /* high = reset held (Q4 inverts) */
    bflb_gpio_init(gpio, PIN_K230_PWR, GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_0);
    bflb_gpio_reset(gpio, PIN_K230_PWR);
    bflb_gpio_init(gpio, PIN_K230_BOOT1, GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_0);
    bflb_gpio_reset(gpio, PIN_K230_BOOT1);       /* NAND boot; a floating BOOT1 sat at 0.5 V (test033) */
    for (unsigned i = 0; i < sizeof(parked); i++)
        park(gpio, parked[i]);
}
