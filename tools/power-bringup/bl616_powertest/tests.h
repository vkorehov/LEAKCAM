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

#if PT_TEST_UART && !PT_TEST_POWER
#error "PT_TEST_UART needs PT_TEST_POWER: the link pins may only be driven while the K230 3V3 is up"
#endif

#endif
