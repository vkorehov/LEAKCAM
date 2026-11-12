/*
 * mTLS to the LEAKCAM server, both firmwares (DESIGN.md 5.3.12 design decision, SECURITY.txt 4, tests/test109):
 * every request to the API port (api.h) runs over one TLS session on wifi_check.c's socket. TLS 1.3 (1.2 the
 * fallback), EC P-256. The trust anchor and the board's identity are in flash, written once at provisioning
 * (`provision tls`, tools/power-bringup/provision_tls.py from make flash): the CA certificate (tls_ca, the one CA
 * that signs the server's and every device's), the board's certificate (tls_crt, CN = its device ID) and its private
 * key (tls_key, never printed or sent). All certificates run 20 years.
 * Dates: checked against the wall clock (wall_clock.c) once the board has one; with no clock (a battery loss, the
 * first boot) the first check ignores ONLY the dates: the chain to the stored CA and the server's name still decide,
 * and that verified answer's X-Time sets the clock (wifi_check.c takes the time only from a verified session).
 */
#ifndef LEAKCAM_TLS_LINK_H
#define LEAKCAM_TLS_LINK_H

#include <stdbool.h>
#include <stddef.h>

#define TLS_SERVER_NAME "leakcam-server"   /* the server certificate's name (tools/pki/leakcam_pki.py), any address */

/* the handshake on a connected socket (one session at a time: wifi_check.c runs one check at a time); false: no
 * session (no credentials stored, a refused certificate, a failed handshake: the reason printed) */
bool tls_link_start(int fd);
int tls_link_send(const void *buf, size_t len);   /* the bytes sent, < 0 an error */
int tls_link_recv(void *buf, size_t len);         /* the bytes read, 0 the end, < 0 an error */
void tls_link_end(void);                          /* close_notify, everything freed */
/* the board holds the CA, its certificate and key (`provision`) */
bool tls_link_provisioned(void);

#endif
