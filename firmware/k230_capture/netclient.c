#define _GNU_SOURCE                    /* strcasestr */
#include "netclient.h"

#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define IO_TIMEOUT_S 10

int net_server(const char *path, char host[64], char port[8])
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    int n = fscanf(f, "%63s %7s", host, port);
    fclose(f);
    return n == 2 ? 0 : -1;
}

static int connect_once(const char *host, const char *port)
{
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0)
        return -1;
    int fd = socket(res->ai_family, res->ai_socktype, 0);
    if (fd >= 0) {
        struct timeval tv = { IO_TIMEOUT_S, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
            close(fd);
            fd = -1;
        }
    }
    freeaddrinfo(res);
    return fd;
}

int net_connect(const char *host, const char *port, int timeout_s)
{
    time_t end = time(NULL) + timeout_s;
    for (;;) {
        int fd = connect_once(host, port);
        if (fd >= 0 || time(NULL) >= end)
            return fd;
        sleep(1);
    }
}

int net_send_all(int fd, const void *p, size_t n)
{
    const char *c = p;
    while (n) {
        ssize_t k = send(fd, c, n, 0);
        if (k <= 0)
            return -1;
        c += k;
        n -= (size_t)k;
    }
    return 0;
}

int http_post(int fd, const char *host, const char *path, const char *type, long len)
{
    char head[256];
    int n = snprintf(head, sizeof(head), "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: %s\r\n"
                 "Content-Length: %ld\r\nConnection: close\r\n\r\n", path, host, type, len);
    return n > 0 && n < (int)sizeof(head) ? net_send_all(fd, head, (size_t)n) : -1;
}

int http_get(int fd, const char *host, const char *path)
{
    char head[256];
    int n = snprintf(head, sizeof(head), "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    return n > 0 && n < (int)sizeof(head) ? net_send_all(fd, head, (size_t)n) : -1;
}

int http_reply_head(int fd, long *len)
{
    char buf[1024];
    size_t used = 0;
    /* a byte at a time, so nothing of the body is read here */
    while (used < sizeof(buf) - 1 && (used < 4 || memcmp(buf + used - 4, "\r\n\r\n", 4) != 0)) {
        if (recv(fd, buf + used, 1, 0) != 1)
            return -1;
        used++;
    }
    buf[used] = 0;
    int status;
    if (sscanf(buf, "HTTP/1.%*d %d", &status) != 1)
        return -1;
    const char *cl = strcasestr(buf, "\r\nContent-Length:");
    *len = cl ? atol(cl + 17) : -1;
    return status;
}

int http_reply(int fd, char *body, size_t cap)
{
    char buf[1024];
    size_t used = 0;
    ssize_t k;
    /* Connection: close, so the reply ends where the stream does */
    while (used < sizeof(buf) - 1 && (k = recv(fd, buf + used, sizeof(buf) - 1 - used, 0)) > 0)
        used += (size_t)k;
    buf[used] = 0;
    int status;
    if (sscanf(buf, "HTTP/1.%*d %d", &status) != 1)
        return -1;
    const char *b = strstr(buf, "\r\n\r\n");
    b = b ? b + 4 : "";
    snprintf(body, cap, "%s", b);
    return status;
}
