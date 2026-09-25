/* Host stand-in: the one WLAN manager call bl616_wifi.c makes. */
#ifndef TEST_STUB_WLAN_MGNT_H
#define TEST_STUB_WLAN_MGNT_H

#include <wlan_dev.h>

rt_err_t rt_wlan_set_mode(const char *dev_name, rt_wlan_mode_t mode);

#endif
