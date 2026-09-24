/*
 * LEAKCAM BL616 Wi-Fi link (NetHub bridge to the K230 over SDIO). See wifi_link.c.
 *
 * Call wifi_link_start() only while the K230's 3V3 is up (after k230_power_on()): it muxes
 * the SDIO pads, whose 10 k pull-ups sit on the K230's switched rail. Call wifi_link_stop()
 * before k230_power_off(). The first start brings up RF, lwIP, NetHub and Wi-Fi; later starts
 * only re-arm the SDIO device for the next K230 boot, the association is kept.
 */
#ifndef LEAKCAM_WIFI_LINK_H
#define LEAKCAM_WIFI_LINK_H

int wifi_link_start(void);
void wifi_link_stop(void);

#endif
