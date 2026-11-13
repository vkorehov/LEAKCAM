/*
 * One HTTP/1.1 POST (whole, or chunked as it is made) or a small GET on POSIX sockets (SAL + lwIP over the BL616's
 * Wi-Fi bridge): the pictures for the server (media.c), the focus session's stream and commands (focus.c). Plain HTTP on the LAN until the K230 has TLS (tests/test109). From tests/firmware/k230_apps/netclient.c,
 * the part the product uses.
 */
#ifndef LEAKCAM_HTTP_POST_H
#define LEAKCAM_HTTP_POST_H

#include <stddef.h>

/* POST body to http://host:port/path with Content-Type type and the extra header line `extra` ("" or "Name: value");
 * connects again until connect_s runs out (DHCP may still be running). The reply's status code, -1: no connection,
 * -2: the send or the reply failed */
int http_post(const char *host, const char *port, const char *path, const char *type, const char *extra,
              const void *body, size_t len, int connect_s);

/* a POST whose length is not known yet (a video as it is encoded): the headers with Transfer-Encoding: chunked; the
 * socket, -1: no connection. http_chunk() sends one piece (0, -1: failed), http_end() the last chunk and returns the
 * reply's status (-2: failed); it always closes the socket */
int http_open(const char *host, const char *port, const char *path, const char *type, const char *extra, int connect_s);
int http_chunk(int fd, const void *p, size_t len);
int http_end(int fd);

/* GET http://host:port/path: the reply's body (small: up to ~900 B, cut to size) into body; the status, -1: no
 * connection, -2: no reply in IO_TIMEOUT_S (10 s: the server's focus command long poll answers within 5 s) */
int http_get(const char *host, const char *port, const char *path, char *body, size_t size, int connect_s);

#endif
