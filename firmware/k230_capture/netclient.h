/*
 * The smallest HTTP/1.1 client the wake needs, on POSIX sockets (SAL + lwIP on RT-Smart): one
 * request per connection, a fixed-length or chunked body, the reply read to the end.
 *
 * The server's address is one line "<host> <port>" in <state dir>/server.
 */
#ifndef LEAKCAM_NETCLIENT_H
#define LEAKCAM_NETCLIENT_H

#include <stddef.h>

/* "<host> <port>" from path; 0 or -1 */
int net_server(const char *path, char host[64], char port[8]);

/* connected TCP socket, retried until timeout_s runs out (DHCP may still be running); -1 */
int net_connect(const char *host, const char *port, int timeout_s);

int net_send_all(int fd, const void *p, size_t n);

/* request head; len < 0: chunked body, sent with http_chunk() */
int http_post(int fd, const char *host, const char *path, const char *type, long len);

/* one chunk of a chunked body; n = 0 ends the body */
int http_chunk(int fd, const void *p, size_t n);

/* status code of the reply (-1 on error); its body, NUL-terminated and cut at cap - 1 */
int http_reply(int fd, char *body, size_t cap);

#endif
