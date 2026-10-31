/*
 * The smallest HTTP/1.1 client the wake needs, on POSIX sockets (SAL + lwIP on RT-Smart): one
 * request per connection, a fixed-length body, the reply read to the end; GET for a download
 * whose body the caller reads itself. rtmp.c uses its TCP
 * connect and send too.
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

/* request head for a body of len bytes, sent after it with net_send_all() */
int http_post(int fd, const char *host, const char *path, const char *type, long len);
int http_get(int fd, const char *host, const char *path);

/* status code of the reply (-1 on error); its body, NUL-terminated and cut at cap - 1 */
int http_reply(int fd, char *body, size_t cap);
/* status code (-1 on error) and Content-Length (-1 if none) of a reply whose body is then read
 * from fd by the caller */
int http_reply_head(int fd, long *len);

#endif
