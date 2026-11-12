#include "k230_power.h"

#include "FreeRTOS.h"
#include "task.h"

#include "bflb_gpio.h"
#include "bflb_mtimer.h"
#include "board_pins.h"
#include "shell.h"

#include <stdio.h>
#include <string.h>

#define RAMP_TIMEOUT_US 100000          /* 3V3 at 90 % ~3 ms after K230_PWR (firmware/sim) */
#define FALL_TIMEOUT_US 1000000
#define OFF_SETTLE_MS   300             /* let 1V8/3V3 discharge fully before the next power-on */

static const uint8_t sense_pins[] = { PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3, PIN_SD_CMD };
#define SENSE_UP ((unsigned)sizeof(sense_pins) - 1)   /* one line may be held by the K230 pad itself */
static bool rails_on;

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

static struct bflb_device_s *gpio(void)
{
    return bflb_device_get_by_name("gpio");
}

static unsigned sense_high(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < sizeof(sense_pins); i++)
        n += bflb_gpio_read(gpio(), sense_pins[i]) ? 1 : 0;
    return n;
}

uint32_t k230_power_on(void)
{
    struct bflb_device_s *g = gpio();
    k230_power_init();      /* the pins set again: after a PDS sleep K230_PWR (IO30) drove nothing, "3V3 did not come up"
                             * on the plug's wake (test113, 2026-11-06); also RSTN held, BOOT1 low */
    for (unsigned i = 0; i < sizeof(sense_pins); i++)
        bflb_gpio_init(g, sense_pins[i], GPIO_INPUT | GPIO_FLOAT | GPIO_SMT_EN);
    bflb_gpio_init(g, PIN_K230_ALIVE, GPIO_INPUT | GPIO_FLOAT | GPIO_SMT_EN);
    uint64_t t0 = bflb_mtimer_get_time_us();
    bflb_gpio_set(g, PIN_K230_PWR);
    rails_on = true;
    while (sense_high() < SENSE_UP) {
        if (bflb_mtimer_get_time_us() - t0 > RAMP_TIMEOUT_US) {
            k230_power_off();
            return 0;
        }
    }
    return (uint32_t)(bflb_mtimer_get_time_us() - t0) | 1u;
}

void k230_power_run(void)
{
    bflb_gpio_reset(gpio(), PIN_K230_RSTN);     /* Q4 inverts: low = reset released */
}

static uint64_t off_ms;                     /* the last power-off (ms since boot): the board cools from then (climate.c) */

uint32_t k230_power_off(void)
{
    struct bflb_device_s *g = gpio();
    bflb_gpio_set(g, PIN_K230_RSTN);
    uint64_t t0 = bflb_mtimer_get_time_us();
    bflb_gpio_reset(g, PIN_K230_PWR);
    rails_on = false;
    off_ms = bflb_mtimer_get_time_ms();
    uint32_t t = 0;
    while (sense_high() != 0) {
        if (bflb_mtimer_get_time_us() - t0 > FALL_TIMEOUT_US)
            break;
    }
    if (sense_high() == 0)
        t = (uint32_t)(bflb_mtimer_get_time_us() - t0) | 1u;
    vTaskDelay(pdMS_TO_TICKS(OFF_SETTLE_MS));
    k230_power_init();                          /* every K230-side pin parked again */
    return t;
}

bool k230_power_is_on(void)
{
    return rails_on;
}

uint64_t k230_power_off_ms(void)
{
    return off_ms;
}

bool k230_alive(void)
{
    return bflb_gpio_read(gpio(), PIN_K230_ALIVE);
}

static void cmd_k230(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "usbboot")) {
        if (rails_on) {
            printf("k230: on already (a session?): k230 off first\r\n");
            return;
        }
        uint32_t ramp = k230_power_on();
        if (!ramp) {
            printf("k230: the 3V3 did not come up\r\n");
            return;
        }
        bflb_gpio_set(gpio(), PIN_K230_BOOT1);      /* the ROM: SD, then USB boot (no SD card) */
        k230_power_run();
        printf("k230: USB boot (3V3 in %lu us): the ROM waits on USB-C 1 (29f1:0230) for flash_nand.sh\r\n",
               (unsigned long)ramp);
        return;
    }
    if (argc > 1 && !strcmp(argv[1], "off")) {
        uint32_t fall = k230_power_off();
        printf("k230: off, BOOT1 low (3V3 fall %lu us)\r\n", (unsigned long)fall);
        return;
    }
    printf("k230: %s; usage: k230 usbboot | off\r\n", rails_on ? "on" : "off");
}
SHELL_CMD_EXPORT_ALIAS(cmd_k230, k230, the K230: usbboot (flash over USB-C 1) | off);
