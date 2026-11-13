#include "http_post.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define IO_TIMEOUT_S 10

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

/* everything, or -1; lwIP out of buffers (two videos at once, 2026-11-12: one upload cut after ~1 s every time) says
 * ENOMEM / EAGAIN at once: tried again until IO_TIMEOUT_S, not taken as a dead connection */
/* one send() at most this much: the program's send() is copied into a kernel buffer of its size first (SAL, a user
 * process; the kernel's own iperf calls lwIP directly): a whole video frame (100-600 KB) in one call failed with
 * ENOMEM on the fragmented kernel heap and kept failing for the same size, the uploads stalled ("Out of memory",
 * test147 2026-11-12), while iperf from the kernel carried two parallel TCP streams at ~28 Mbit/s (14 + 14) */
#define SEND_MAX (16 * 1024)

static int send_all(int fd, const void *p, size_t n)
{
    const char *c = p;
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (n) {
        ssize_t k = send(fd, c, n < SEND_MAX ? n : SEND_MAX, 0);
        if (k > 0) {
            c += k;
            n -= (size_t)k;
            t0.tv_sec = 0;
            continue;
        }
        clock_gettime(CLOCK_MONOTONIC, &t);
        if (!t0.tv_sec)
            t0 = t;
        if (k < 0 && (errno == ENOMEM || errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) &&
            t.tv_sec - t0.tv_sec < IO_TIMEOUT_S) {
            usleep(5000);
            continue;
        }
        fprintf(stderr, "http: send failed: %s\n", k < 0 ? strerror(errno) : "closed");
        return -1;
    }
    return 0;
}

static int connect_until(const char *host, const char *port, int connect_s)
{
    struct timespec end, now;
    clock_gettime(CLOCK_MONOTONIC, &end);
    end.tv_sec += connect_s;
    for (;;) {
        int fd = connect_once(host, port);
        if (fd >= 0)
            return fd;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec >= end.tv_sec)
            return -1;
        usleep(250000);
    }
}

/* the reply's status code, -2: none */
static int status_of(int fd)
{
    char r[64];                                     /* "HTTP/1.1 200 OK" is all that is needed */
    ssize_t k = recv(fd, r, sizeof(r) - 1, 0);
    if (k <= 12)
        return -2;
    r[k] = 0;
    return strncmp(r, "HTTP/1.", 7) ? -2 : atoi(r + 9);
}

int http_open(const char *host, const char *port, const char *path, const char *type, const char *extra, int connect_s)
{
    int fd = connect_until(host, port, connect_s);
    if (fd < 0)
        return -1;
    char head[384];
    int n = snprintf(head, sizeof(head), "POST %s HTTP/1.1\r\nHost: %s:%s\r\nContent-Type: %s\r\n"
                     "Transfer-Encoding: chunked\r\n%s%sConnection: close\r\n\r\n", path, host, port, type, extra,
                     *extra ? "\r\n" : "");
    if (n <= 0 || n >= (int)sizeof(head) || send_all(fd, head, (size_t)n)) {
        close(fd);
        return -1;
    }
    return fd;
}

int http_chunk(int fd, const void *p, size_t len)
{
    char h[16];
    int n = snprintf(h, sizeof(h), "%zx\r\n", len);
    return len && (send_all(fd, h, (size_t)n) || send_all(fd, p, len) || send_all(fd, "\r\n", 2)) ? -1 : 0;
}

int http_end(int fd)
{
    int status = send_all(fd, "0\r\n\r\n", 5) ? -2 : status_of(fd);
    close(fd);
    return status;
}

int http_post(const char *host, const char *port, const char *path, const char *type, const char *extra,
              const void *body, size_t len, int connect_s)
{
    int fd = connect_until(host, port, connect_s);
    if (fd < 0)
        return -1;
    char head[384];
    int n = snprintf(head, sizeof(head), "POST %s HTTP/1.1\r\nHost: %s:%s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                     "%s%sConnection: close\r\n\r\n", path, host, port, type, len, extra, *extra ? "\r\n" : "");
    int status = -2;
    if (n > 0 && n < (int)sizeof(head) && send_all(fd, head, (size_t)n) == 0 && send_all(fd, body, len) == 0)
        status = status_of(fd);
    close(fd);
    return status;
}

int http_get(const char *host, const char *port, const char *path, char *body, size_t size, int connect_s)
{
    int fd = connect_until(host, port, connect_s);
    if (fd < 0)
        return -1;
    char head[256];
    int n = snprintf(head, sizeof(head), "GET %s HTTP/1.1\r\nHost: %s:%s\r\nConnection: close\r\n\r\n", path, host,
                     port);
    if (n <= 0 || n >= (int)sizeof(head) || send_all(fd, head, (size_t)n)) {
        close(fd);
        return -2;
    }
    char r[1024];                                   /* the headers and a small body (a focus command's JSON) */
    size_t got = 0;
    for (;;) {                                      /* Connection: close: the answer ends with the connection */
        ssize_t k = recv(fd, r + got, sizeof(r) - 1 - got, 0);
        if (k <= 0 || (got += (size_t)k) >= sizeof(r) - 1)
            break;
    }
    close(fd);
    r[got] = 0;
    if (got < 12 || strncmp(r, "HTTP/1.", 7))
        return -2;
    const char *b = strstr(r, "\r\n\r\n");
    snprintf(body, size, "%s", b ? b + 4 : "");
    return atoi(r + 9);
}

