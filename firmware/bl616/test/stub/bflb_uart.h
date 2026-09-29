/* Minimal stand-in for the SDK UART API, enough for k230_link.c on a host. */
#pragma once
#include "bl616_sdk_stub.h"
int bflb_uart_getchar(struct bflb_device_s *d);
int bflb_uart_putchar(struct bflb_device_s *d, int c);
