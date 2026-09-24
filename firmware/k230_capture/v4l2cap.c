#define _GNU_SOURCE
#include "v4l2cap.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static int xioctl(int fd, unsigned long req, void *arg)
{
    int r;
    do
        r = ioctl(fd, req, arg);
    while (r < 0 && errno == EINTR);
    return r;
}

static int by_node(const void *a, const void *b)
{
    const struct cap_cam *x = a, *y = b;
    return (int)x->node - (int)y->node;
}

static int open_and_format(struct cap_cam *c, unsigned width, unsigned height)
{
    char path[32];
    snprintf(path, sizeof(path), "/dev/video%u", c->node);
    c->fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (c->fd < 0) {
        fprintf(stderr, "v4l2cap: open %s: %s\n", path, strerror(errno));
        return -1;
    }
    struct v4l2_format f;
    memset(&f, 0, sizeof(f));
    f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int retry;
    for (retry = 0; retry < 5; retry++) {         /* G_FMT creates the ISP pipeline */
        if (retry)
            usleep(100000u * (unsigned)retry);
        if (xioctl(c->fd, VIDIOC_G_FMT, &f) == 0)
            break;
    }
    if (retry == 5) {
        fprintf(stderr, "v4l2cap: %s G_FMT: %s\n", path, strerror(errno));
        return -1;
    }
    f.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
    f.fmt.pix.width = width & ~7u;
    f.fmt.pix.height = height & ~1u;
    if (xioctl(c->fd, VIDIOC_S_FMT, &f) < 0 || xioctl(c->fd, VIDIOC_G_FMT, &f) < 0) {
        fprintf(stderr, "v4l2cap: %s S_FMT %ux%u NV12: %s\n", path, width, height, strerror(errno));
        return -1;
    }
    c->width = f.fmt.pix.width;
    c->height = f.fmt.pix.height;
    c->stride = f.fmt.pix.bytesperline ? f.fmt.pix.bytesperline : f.fmt.pix.width;
    return 0;
}

static int map_and_queue(struct cap_cam *c)
{
    struct v4l2_requestbuffers rb;
    memset(&rb, 0, sizeof(rb));
    rb.count = CAP_NBUF;
    rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rb.memory = V4L2_MEMORY_MMAP;
    if (xioctl(c->fd, VIDIOC_REQBUFS, &rb) < 0 || rb.count < 2) {
        fprintf(stderr, "v4l2cap: video%u REQBUFS: %s\n", c->node, strerror(errno));
        return -1;
    }
    for (unsigned i = 0; i < rb.count && i < CAP_NBUF; i++) {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof(b));
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        if (xioctl(c->fd, VIDIOC_QUERYBUF, &b) < 0)
            return -1;
        c->len[i] = b.length;
        c->buf[i] = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, c->fd, b.m.offset);
        if (c->buf[i] == MAP_FAILED) {
            c->buf[i] = NULL;
            return -1;
        }
        if (xioctl(c->fd, VIDIOC_QBUF, &b) < 0)
            return -1;
    }
    c->luma = malloc((size_t)c->width * c->height);
    return c->luma ? 0 : -1;
}

int cap_open_all(struct cap_cam *cams, int n, unsigned width, unsigned height)
{
    for (int i = 0; i < n; i++)
        cams[i].fd = -1;
    qsort(cams, (size_t)n, sizeof(cams[0]), by_node);   /* ISP ports probe in node order */
    for (int i = 0; i < n; i++)
        if (open_and_format(&cams[i], width, height) < 0)
            return -1;
    for (int i = 0; i < n; i++)
        if (map_and_queue(&cams[i]) < 0)
            return -1;
    return 0;
}

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

int cap_grab_all(struct cap_cam *cams, int n, unsigned settle, int timeout_ms)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (int i = 0; i < n; i++) {
        cams[i].frames = 0;
        if (xioctl(cams[i].fd, VIDIOC_STREAMON, &type) < 0) {
            fprintf(stderr, "v4l2cap: video%u STREAMON: %s\n", cams[i].node, strerror(errno));
            return -1;
        }
    }
    uint64_t deadline = now_ms() + (uint64_t)timeout_ms;
    int done = 0;
    while (done < n) {
        struct pollfd p[CAP_MAX_CAMS];
        for (int i = 0; i < n; i++)
            p[i] = (struct pollfd){ .fd = cams[i].fd,
                                    .events = cams[i].frames > settle ? 0 : POLLIN };
        int left = (int)(deadline - now_ms());
        if (left <= 0 || poll(p, (nfds_t)n, left) <= 0) {
            fprintf(stderr, "v4l2cap: timeout, %d of %d cameras delivered\n", done, n);
            return -1;
        }
        for (int i = 0; i < n; i++) {
            if (!(p[i].revents & POLLIN))
                continue;
            struct v4l2_buffer b;
            memset(&b, 0, sizeof(b));
            b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            b.memory = V4L2_MEMORY_MMAP;
            if (xioctl(cams[i].fd, VIDIOC_DQBUF, &b) < 0)
                continue;
            if (++cams[i].frames == settle + 1) {
                /* NV12: the first width x height (row stride) bytes are luminance */
                const uint8_t *y = cams[i].buf[b.index];
                for (unsigned r = 0; r < cams[i].height; r++)
                    memcpy(cams[i].luma + (size_t)r * cams[i].width,
                           y + (size_t)r * cams[i].stride, cams[i].width);
                done++;
            }
            xioctl(cams[i].fd, VIDIOC_QBUF, &b);
        }
    }
    for (int i = 0; i < n; i++)
        xioctl(cams[i].fd, VIDIOC_STREAMOFF, &type);
    return 0;
}

void cap_close_all(struct cap_cam *cams, int n)
{
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < CAP_NBUF; k++)
            if (cams[i].buf[k])
                munmap(cams[i].buf[k], cams[i].len[k]);
        if (cams[i].fd >= 0)
            close(cams[i].fd);
        free(cams[i].luma);
        cams[i].luma = NULL;
        cams[i].fd = -1;
    }
}
