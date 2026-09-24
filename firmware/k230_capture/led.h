/*
 * The two LED chains (TPS61161 U14 white on J1, U15 IR on J2), each dimmed by the PWM duty on
 * its CTRL pin: I_LED = duty x 200 mV / R_FB. Implemented per platform (led_linux.c, led_rtsmart.c).
 */
#ifndef LEAKCAM_LED_H
#define LEAKCAM_LED_H

#include <stdbool.h>

enum { LED_WHITE, LED_IR };

/* 25 kHz: inside the TPS61161's 5-100 kHz dimming range, and every low phase (< 40 us) is far
 * shorter than the 260 us that would select EasyScale mode after enable, or the 2.5 ms that
 * shuts the driver down. Both channels share one K230 PWM controller, so one period for both. */
#define LED_PWM_PERIOD_NS 40000
#define LED_SOFTSTART_US  10000   /* TPS61161: 32 steps x 213 us = 6.8 ms to full current */

/* white: PWM1 = GPIO61 = LED_CTL2 -> U14, IR: PWM0 = GPIO60 = LED_CTL1 -> U15 */
extern const unsigned led_channel[2];
extern const char *const led_name[2];

/* on: both chains at percent[LED_WHITE] / percent[LED_IR] (0 = that chain off), then wait for the
 * soft start; off: duty 0 and disabled, CTRL low, the drivers shut down. pwm_dev selects the
 * PWM controller (Linux: pwmchip number). Returns 0 or -1 (message on stderr). */
int led_set(bool on, const unsigned percent[2], unsigned pwm_dev);

#endif
