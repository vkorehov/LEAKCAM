/*
 * LEAKCAM BL616 (Ai-M62-CBS, U11) pin map, from WIFI.SchDoc / POWER.SchDoc.
 *
 * Every pin that touches the K230 side ends in a 10 k pull-up to the SWITCHED 3V3 rail
 * (R34 R36 R43 R44 R57 R65 R70) or goes straight into a K230 pad. While the K230 is off,
 * driving any of them high pushes current backwards into the dead 3V3 rail and partially
 * powers the K230 IO banks, both cameras and the CH340X (see firmware/sim). They must be
 * parked (analog, no pull) before K230_PWR goes low and stay parked until 3V3 is up.
 */
#ifndef LEAKCAM_BOARD_PINS_H
#define LEAKCAM_BOARD_PINS_H

#include "bflb_gpio.h"

/*
 * Pin map for the NEXT board spin. Two nets on U11 change against the Sep 2026 fab:
 *   PGOOD    U11.39 (IO27) -> U11.7  (IO03 = ADC_CH3, watched by ACOMP0, can wake HBN)
 *   K230_PWR U11.7  (IO03) -> U11.43 (IO30, plain output; R19 still pulls it low in HBN)
 * Reason: a USB plug must wake the BL616 from HBN. HBN wakes only on GPIO16-19 (not brought out
 * / used by the 32 kHz crystal), RTC, and ACOMP0/1, and ACOMP can select ADC_CH0-7 only. IO27 is
 * ADC_CH10. IO03 is ADC_CH3, the same ACOMP0 pairing the SDK's bl616dk board uses.
 */

/* K230 power control */
#define PIN_K230_RSTN   GPIO_PIN_0   /* IO00 -> Q4 gate, R76 100k pull-up: HIGH = K230 HELD IN RESET */
#define PIN_K230_ALIVE  GPIO_PIN_1   /* IO01 <- K230 GPIO2 (JTAG_TCK at reset), R32 100k pull-up to 3V3 */
#define PIN_K230_PWR    GPIO_PIN_30  /* IO30 -> U6 EN via R51, R19 100k pull-down: HIGH = K230 rails on */

/* USB input power: BQ24072 PGOOD (U1.7, open drain, R23 100k to 3V3_SLEEP), LOW = USB power good */
#define PIN_USB_PGOOD   GPIO_PIN_3   /* IO03 = ADC_CH3 -> ACOMP0 */

/* leak probes S1..S3: 1 M / 1 M divider from 3V3_SLEEP, 1.65 V dry. GPIO20 = ADC_CH0 = ACOMP ADC0 */
#define PIN_LEAK_SENS   GPIO_PIN_20

/* K230 link, UART0: BL616 TXD GPIO21 -> K230 GPIO41 (UART1_RXD), RXD GPIO22 <- K230 GPIO40 (UART1_TXD).
 * These are also the SDK's default console pins and the BootROM UART; the console is moved to USB
 * CDC (PR1) in defconfig so boot logs never reach the K230 or back-feed its rail. */
#define PIN_LINK_TX     GPIO_PIN_21
#define PIN_LINK_RX     GPIO_PIN_22

/* SDIO slave to K230 MMC0 (hosted Wi-Fi). Not used by this PoC, but parked with the rest. */
#define PIN_SD_D2       GPIO_PIN_10
#define PIN_SD_D3       GPIO_PIN_11
#define PIN_SD_CMD      GPIO_PIN_12
#define PIN_SD_CLK      GPIO_PIN_13
#define PIN_SD_D0       GPIO_PIN_14
#define PIN_SD_D1       GPIO_PIN_15

#define K230_SIDE_PINS { PIN_LINK_TX, PIN_LINK_RX, PIN_SD_D2, PIN_SD_D3, PIN_SD_CMD, PIN_SD_CLK, PIN_SD_D0, PIN_SD_D1 }

#endif
