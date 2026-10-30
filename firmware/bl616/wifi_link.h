/*
 * LEAKCAM BL616 Wi-Fi link (NetHub bridge to the K230 over SDIO). See wifi_link.c.
 *
 * Call wifi_link_start() only while the K230's 3V3 is up (after k230_power_on()): it muxes
 * the SDIO pads, whose 10 k pull-ups sit on the K230's switched rail. Call wifi_link_stop()
 * before k230_power_off(). RF (rfparam_init) and easyflash must be up already: BLE shares them.
 * The first start brings up lwIP, NetHub and Wi-Fi; later starts only re-arm the SDIO device
 * for the next K230 boot, the association is kept.
 *
 * On USB power the radio has two users that must not overlap: a K230 session (the bridge, the
 * K230 does DHCP with the BL616's MAC) and the BLE credential check (wifi_check.c, the BL616
 * does DHCP itself). Each claims the radio first; the check wins (BLE first).
 */
#ifndef LEAKCAM_WIFI_LINK_H
#define LEAKCAM_WIFI_LINK_H

#include <stdbool.h>

enum wifi_owner { WIFI_OWNER_NONE, WIFI_OWNER_K230, WIFI_OWNER_CHECK };

/* take the radio for who: false while the other user has it. BLE first: a check takes it from a K230 session
 * too, which gets it back (the bridge rejoins) at the check's release */
bool wifi_link_claim(enum wifi_owner who);
void wifi_link_release(enum wifi_owner who);
/* new credentials were stored: the bridge's next start joins them (main.c restarts a running K230 session) */
void wifi_link_credentials_changed(void);

int wifi_link_start(void);
void wifi_link_stop(void);
/* an SSID from BLE provisioning is stored (easyflash must be up) */
bool wifi_link_has_credentials(void);

/* for wifi_check.c, with the radio claimed: bring Wi-Fi up without the bridge (or keep it, if a
 * K230 session started it), deliver every received frame to the BL616's own lwIP, and pass the
 * Wi-Fi manager's events (CODE_WIFI_ON_*) to ev. ev gets CODE_WIFI_ON_MGMR_DONE once the
 * manager can take requests. wifi_link_local_end() undoes it. */
void wifi_link_local_begin(void (*ev)(int code));
/* autoconnect off and disconnect until the supplicant has no scan or join of its own running (usually 0-3 s, at most
 * 10 s); false if it stayed busy. Never call wifi_mgmr_sta_connect() before it said true: into a busy station it
 * never returns. Blocks: not from the Wi-Fi event callback */
bool wifi_link_sta_leave(void);
void wifi_link_local_end(void);
/* setup on USB: a phone connected over BLE and a check will likely follow - Wi-Fi up now, in local mode with its events
 * dropped, so the check starts at once (Wi-Fi start took 1.3 s of the user's wait, tests/test106). Nothing when a
 * K230 session or a check has the radio. Blocks ~1 s: its own task. wifi_link_cool() hands it back when the phone left
 * without a check; a check's wifi_link_local_begin() takes it over. */
void wifi_link_warm(void);
void wifi_link_cool(void);

#endif
