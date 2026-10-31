/* Host test of rtmp.c: publishes a real 3-frame H.265 clip (test/sample.h265, x265 160x120 Main:
 * VPS SPS PPS SEI IDR_N_LP TRAIL TRAIL) and a quiet 400 Hz tone, Opus-encoded as leakcam_stream
 * does it, to rtmp://127.0.0.1:<port>/leakcam/cam0, where test_rtmp.py runs `ffmpeg -listen 1`
 * (7.1 or later: Enhanced RTMP v2 audio) and checks what it received.
 *   test_rtmp <sample.h265> <port> */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "opus.h"
#include "rtmp.h"

#define RATE  16000
#define FRAME (RATE / 25)                       /* 40 ms, the AI frame of leakcam_stream */

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(int argc, char **argv)
{
    int err;
    OpusEncoder *enc = opus_encoder_create(RATE, 1, OPUS_APPLICATION_AUDIO, &err);
    if (!enc)
        return 2;
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(3));
    opus_int32 lookahead = 0;
    opus_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&lookahead));

    static uint8_t clip[8192];
    FILE *in = argc == 3 ? fopen(argv[1], "rb") : NULL;
    if (!in)
        return 2;
    size_t n = fread(clip, 1, sizeof(clip), in);
    fclose(in);

    static struct rtmp r;
    if (rtmp_publish(&r, "127.0.0.1", argv[2], "leakcam", "cam0", 10)) {
        printf("FAIL: publish\n");
        return 1;
    }
    CHECK(rtmp_opus_header(&r, (unsigned)lookahead * 48000 / RATE, RATE) == 0, "opus header");
    /* access units: every NAL up to and including a picture slice (H.265 VCL types 0..31) */
    size_t au_start = 0, au_count = 0;
    for (size_t i = 0; i + 4 <= n; i++) {
        uint8_t t = (clip[i + 3] >> 1) & 0x3F;
        if (clip[i] || clip[i + 1] || clip[i + 2] != 1 || t >= 32)
            continue;
        size_t end = i + 3;
        while (end + 3 <= n && !(clip[end] == 0 && clip[end + 1] == 0 && clip[end + 2] == 1))
            end++;
        if (end + 3 > n)
            end = n;
        uint32_t ms = (uint32_t)au_count * 100;               /* 10 fps */
        CHECK(rtmp_video(&r, clip + au_start, end - au_start, ms) == 0, "video %zu", au_count);
        for (uint32_t a = ms; a < ms + 100; a += 40) {     /* a quiet tone, like a small leak */
            opus_int16 pcm[FRAME];
            uint8_t pkt[400];
            for (unsigned s = 0; s < FRAME; s++)
                pcm[s] = (opus_int16)(800 * sin(2 * M_PI * 400 * (a * (RATE / 1000) + s) / RATE));
            int n = opus_encode(enc, pcm, FRAME, pkt, sizeof(pkt));
            CHECK(n > 0 && rtmp_audio(&r, pkt, (size_t)n, a) == 0, "audio %u (%d bytes)", a, n);
        }
        au_count++;
        au_start = end;
        i = end - 1;
    }
    rtmp_close(&r);
    opus_encoder_destroy(enc);
    CHECK(au_count == 3 && r.config_sent && r.started, "%zu access units, config %d", au_count, r.config_sent);
    return fails ? 1 : 0;
}
