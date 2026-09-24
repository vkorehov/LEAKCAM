/* LED chains on Linux: sysfs PWM (/sys/class/pwm/pwmchipN/pwmM). */
#define _GNU_SOURCE
#include "led.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

const unsigned led_channel[2] = { 1, 0 };
const char *const led_name[2] = { "white", "ir" };

static int sysfs_write(const char *path, const char *val)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    ssize_t n = write(fd, val, strlen(val));
    close(fd);
    return n == (ssize_t)strlen(val) ? 0 : -1;
}

static int pwm_attr(unsigned pwm_chip, unsigned ch, const char *attr, unsigned long v)
{
    char path[96], val[24];
    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip%u/pwm%u/%s", pwm_chip, ch, attr);
    snprintf(val, sizeof(val), "%lu", v);
    return sysfs_write(path, val);
}

int led_set(bool on, const unsigned percent[2], unsigned pwm_chip)
{
    int rc = 0;
    for (int i = 0; i < 2; i++) {
        unsigned ch = led_channel[i];
        char dir[64], exp[64], num[8];
        snprintf(dir, sizeof(dir), "/sys/class/pwm/pwmchip%u/pwm%u", pwm_chip, ch);
        if (access(dir, F_OK) != 0) {
            snprintf(exp, sizeof(exp), "/sys/class/pwm/pwmchip%u/export", pwm_chip);
            snprintf(num, sizeof(num), "%u", ch);
            if (sysfs_write(exp, num) < 0) {
                fprintf(stderr, "led %s: export pwm%u on pwmchip%u failed (pwm0 node disabled, or "
                                "GPIO6%u not muxed as PWM%u?)\n", led_name[i], ch, pwm_chip, ch, ch);
                rc = -1;
                continue;
            }
            usleep(10000);                        /* udev creates the attributes asynchronously */
        }
        unsigned pct = on ? percent[i] : 0;
        /* period before duty: duty may never exceed the period the driver holds */
        if (pwm_attr(pwm_chip, ch, "period", LED_PWM_PERIOD_NS) < 0 ||
            pwm_attr(pwm_chip, ch, "duty_cycle", (unsigned long)LED_PWM_PERIOD_NS * pct / 100u) < 0 ||
            pwm_attr(pwm_chip, ch, "enable", on && pct ? 1 : 0) < 0) {
            fprintf(stderr, "led %s: configuring pwm%u failed: %s\n", led_name[i], ch, strerror(errno));
            rc = -1;
        }
    }
    if (on && rc == 0)
        usleep(LED_SOFTSTART_US);
    return rc;
}
