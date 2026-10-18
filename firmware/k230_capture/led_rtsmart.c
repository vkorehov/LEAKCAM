/*
 * LED chains on RT-Smart: /dev/pwm of the CanMV rtsmart kernel (bsp/maix3 interdrv/pwm).
 * One node for PWM0..5; each ioctl takes struct rt_pwm_configuration {channel, period ns,
 * pulse ns}. PWM0..2 share one period register, so white (PWM1) and IR (PWM0) share 25 kHz.
 * pulse = 0 drives duty 0 (CTRL low), pulse >= period duty 100 %.
 * Pin mux (GPIO60 = PWM0, GPIO61 = PWM1, sel 1) comes from the board's pinmux_config.c.
 * pwm_dev is unused: there is one PWM node.
 */
#include "led.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* same encoding as the kernel driver and rtsmart_hal/drivers/pwm/drv_pwm.c */
#define KD_PWM_CMD_ENABLE  _IOW('P', 0, int)
#define KD_PWM_CMD_DISABLE _IOW('P', 1, int)
#define KD_PWM_CMD_SET_CFG _IOW('P', 2, int)

struct rt_pwm_configuration {
    uint32_t channel;
    uint32_t period;     /* ns */
    uint32_t pulse;      /* ns, <= period */
};

const unsigned led_channel[2] = { 1, 0 };
const char *const led_name[2] = { "white", "ir" };

int led_set(bool on, const unsigned percent[2], unsigned pwm_dev)
{
    (void)pwm_dev;
    int fd = open("/dev/pwm", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "led: open /dev/pwm: %s\n", strerror(errno));
        return -1;
    }
    int rc = 0;
    /* Two passes: the channels that stay off first, then the ones that turn on. PWM0..2 share one
     * counter, and a channel's output is high while the counter is at or above its compare
     * register, which is 0 after reset: enabling white first started the counter while IR's
     * compare was still 0, so U15 ran the IR chain at full current until IR was disabled (seen on
     * rev 1, 2026-10-18). DISABLE sets the compare to its maximum: that channel stays low. */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < 2; i++) {
            unsigned pct = on ? percent[i] : 0;
            if ((pass == 0) != (pct == 0))
                continue;
            struct rt_pwm_configuration c = {
                .channel = led_channel[i],
                .period = LED_PWM_PERIOD_NS,
                .pulse = (uint32_t)((uint64_t)LED_PWM_PERIOD_NS * pct / 100u),
            };
            /* off: duty 0 first so CTRL is low before the channel stops */
            if (ioctl(fd, KD_PWM_CMD_SET_CFG, &c) < 0 ||
                ioctl(fd, pct ? KD_PWM_CMD_ENABLE : KD_PWM_CMD_DISABLE, &c) < 0) {
                fprintf(stderr, "led %s: PWM%u configuration failed: %s\n", led_name[i],
                        led_channel[i], strerror(errno));
                rc = -1;
            }
        }
    }
    close(fd);
    if (on && rc == 0)
        usleep(LED_SOFTSTART_US);
    return rc;
}
