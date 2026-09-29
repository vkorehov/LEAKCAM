/* Minimal stand-in for the SDK UART API, enough for k230_link.c on a host. */
#pragma once
struct bflb_device_s { int dummy; };
struct bflb_device_s *bflb_device_get_by_name(const char *n);
int bflb_uart_getchar(struct bflb_device_s *d);
int bflb_uart_putchar(struct bflb_device_s *d, int c);
