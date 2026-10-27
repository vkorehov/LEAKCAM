/*
 * Which hardware tests the BL616 bring-up firmware contains. Set one to 0 to leave it out
 * (its commands and its part of pt_mon disappear), then rebuild.
 */
#ifndef PT_TESTS_H
#define PT_TESTS_H

#define PT_TEST_POWER  1   /* pt_status pt_on pt_off pt_run pt_hold pt_cycle: K230 rails, 3V3 timing  */
#define PT_TEST_UART   1   /* pt_uart: periodic message on UART0 (the K230 link), echo of what comes back */
#define PT_TEST_I2C    1   /* pt_i2c: bus scan and AHT20 humidity/temperature (3V3_SLEEP, always on)   */
#define PT_TEST_USB    1   /* pt_usb: USB cable detection (BQ24072 PGOOD), change reported live         */
#define PT_TEST_LEAK   1   /* pt_leak: leak probe node voltage and probe resistance (S1..S3)            */
#define PT_TEST_SDIO   1   /* pt_sdio: SDIO lines to K230 MMC0: edges while it probes, driven patterns  */
#define PT_TEST_SDU    1   /* pt_sdu: SDIO device echo (the SDK's sdio2_test) for the K230 bus speed test */
#define PT_TEST_SLEEP  1   /* pt_sleep: K230 off, HBN level 0, RTC wake after <s> (test031 sleep current) */
#define PT_TEST_PINS   1   /* pt_pins: every GPIO's live configuration from the GLB registers (test033)      */
#define PT_TEST_TIMING 1   /* pt_time: test021 timings (wake -> main, rails, AHT20, ADC, easyflash key)   */
#define PT_TEST_WIFI   1   /* pt_wifi: test100 step 1, the BL616 radio alone: scan, join + DHCP, status (pt_wifi.c) */
#define PT_TEST_BLE    1   /* pt_ble: test040, the product's BLE provisioning of the Wi-Fi credentials (pt_ble.c) */

#include <stdbool.h>
bool rails_are_on(void);              /* main.c: the K230 rails are commanded on */
void pt_wifi_rails_off(void);         /* pt_wifi.c: stop the NetHub bridge (wifi_link_stop) before the rails drop */
#include <stdint.h>
bool pt_wifi_poll_cycle(uint32_t t[4]);  /* pt_wifi.c: one BL616-only server check (pt_poll) */
bool pt_radio_init(void);             /* pt_wifi.c: RF parameters once per boot (BLE and Wi-Fi) */
int pt_ef_once(void);                 /* pt_wifi.c: easyflash once per boot */

#if PT_TEST_WIFI && !PT_TEST_POWER
#error "PT_TEST_WIFI needs PT_TEST_POWER: the bridge's SDIO pads may only be driven while the K230 3V3 is up"
#endif
#if PT_TEST_BLE && !(PT_TEST_WIFI && PT_TEST_USB)
#error "PT_TEST_BLE needs PT_TEST_WIFI (radio, the credential check) and PT_TEST_USB (pairing only on USB power)"
#endif
#if PT_TEST_UART && !PT_TEST_POWER
#error "PT_TEST_UART needs PT_TEST_POWER: the link pins may only be driven while the K230 3V3 is up"
#endif
#if PT_TEST_SDU && !(PT_TEST_SDIO && PT_TEST_POWER)
#error "PT_TEST_SDU needs PT_TEST_SDIO (pad parking) and PT_TEST_POWER (the K230 3V3 must be up)"
#endif
#if PT_TEST_SLEEP && !PT_TEST_POWER
#error "PT_TEST_SLEEP needs PT_TEST_POWER: the K230 rails go off before the BL616 hibernates"
#endif
#if PT_TEST_TIMING && !(PT_TEST_SLEEP && PT_TEST_I2C && PT_TEST_LEAK)
#error "PT_TEST_TIMING times the RTC wake (PT_TEST_SLEEP), the AHT20 (PT_TEST_I2C) and the probe ADC (PT_TEST_LEAK)"
#endif

#endif
