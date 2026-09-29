/* Host stand-in for leakcam_stream's push mode: the same command line and the same upload per
 * camera (chunked POST /v1/video?cam=<N>, an FLV of H.264 plus G.711 mu-law), made from
 * test/sample.h264 and a 400 Hz tone instead of the cameras, encoder and microphone. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "flv.h"
#include "netclient.h"

static int to_upload(void *ctx, const void *p, size_t n)
{
    return http_chunk(*(int *)ctx, p, n);
}

/* the clip as leakcam_stream would send it: one access unit per video tag, 40 ms audio between */
static int send_clip(int fd, const uint8_t *clip, size_t n)
{
    struct flv_video v = { .config_sent = false };
    size_t au_start = 0;
    uint32_t ms = 0;
    if (flv_header(to_upload, &fd))
        return -1;
    for (size_t i = 0; i + 4 <= n; i++) {
        uint8_t t = clip[i + 3] & 0x1F;
        if (clip[i] || clip[i + 1] || clip[i + 2] != 1 || (t != 1 && t != 5))
            continue;
        size_t end = i + 3;
        while (end + 3 <= n && !(clip[end] == 0 && clip[end + 1] == 0 && clip[end + 2] == 1))
            end++;
        if (end + 3 > n)
            end = n;
        if (flv_video(&v, to_upload, &fd, clip + au_start, end - au_start, ms))
            return -1;
        for (uint32_t a = ms; a < ms + 100; a += 40) {
            uint8_t u[320];
            for (unsigned s = 0; s < sizeof(u); s++)
                u[s] = flv_ulaw((int16_t)(8000 * sin(2 * M_PI * 400 * (a * 8 + s) / FLV_AUDIO_RATE)));
            if (flv_audio(to_upload, &fd, u, sizeof(u), a))
                return -1;
        }
        ms += 100;
        au_start = end;
        i = end - 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    char host[64], port[8];
    if (argc != 5 || strcmp(argv[1], "-p") || strcmp(argv[3], "-t") ||
        sscanf(argv[2], "%63[^:]:%7s", host, port) != 2)
        return 2;
    static uint8_t clip[8192];
    FILE *f = fopen(SAMPLE_H264, "rb");
    size_t n = f ? fread(clip, 1, sizeof(clip), f) : 0;
    if (f)
        fclose(f);
    if (!n)
        return 1;
    for (int cam = 0; cam < 2; cam++) {
        char path[32], reply[128];
        snprintf(path, sizeof(path), "/v1/video?cam=%d", cam);
        int fd = net_connect(host, port, 5);
        if (fd < 0 || http_post(fd, host, path, "video/x-flv", -1) < 0 || send_clip(fd, clip, n) ||
            http_chunk(fd, NULL, 0) < 0 || http_reply(fd, reply, sizeof(reply)) != 200)
            return 1;
        close(fd);
    }
    printf("seconds=%s\n", argv[4]);
    return 0;
}
