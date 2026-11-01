/*
 * The setup firmware's Wi-Fi: the wifi_link.h interface without the NetHub bridge (wifi_setup.c).
 */
#ifndef LEAKCAM_WIFI_SETUP_H
#define LEAKCAM_WIFI_SETUP_H

/* lwIP and the Wi-Fi task, at boot (setup_boot.c); wifi_check.c then runs through wifi_link.h */
void wifi_setup_start(void);

#endif
