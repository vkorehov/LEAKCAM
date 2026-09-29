/* Host test of flv.c: a real 3-frame H.264 clip (test/sample.h264, x264 160x120, SPS PPS SEI IDR
 * P P) and a 400 Hz tone in 40 ms mu-law frames become one FLV, which test_flv.py then checks
 * with ffprobe/ffmpeg: an h264 and a pcm_mulaw 8 kHz mono stream that decode without errors.
 * The mu-law encoder is checked against the ITU reference values first. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "flv.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static int to_file(void *ctx, const void *p, size_t n)
{
    return fwrite(p, 1, n, ctx) == n ? 0 : -1;
}

int main(int argc, char **argv)
{
    static const struct { int16_t pcm; uint8_t u; } ref[] = {
        { 0, 0xFF }, { -1, 0x7F }, { 100, 0xF2 }, { 1000, 0xCE }, { -1000, 0x4E },
        { -5000, 0x2B }, { 32767, 0x80 }, { -32768, 0x00 },
    };
    for (unsigned i = 0; i < sizeof(ref) / sizeof(ref[0]); i++)
        CHECK(flv_ulaw(ref[i].pcm) == ref[i].u, "mu-law %d -> %02x, want %02x", ref[i].pcm,
              flv_ulaw(ref[i].pcm), ref[i].u);

    static uint8_t clip[8192];
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
    if (argc != 3 || !in || !out)
        return 2;
    size_t n = fread(clip, 1, sizeof(clip), in);
    fclose(in);

    /* access units: every NAL up to and including a slice (types 1, 5) */
    size_t au_start = 0, au_count = 0;
    struct flv_video v = { .config_sent = false };
    CHECK(flv_header(to_file, out) == 0, "header");
    for (size_t i = 0; i + 4 <= n; i++) {
        if (clip[i] || clip[i + 1] || clip[i + 2] != 1 || ((clip[i + 3] & 0x1F) != 1 && (clip[i + 3] & 0x1F) != 5))
            continue;
        size_t end = i + 3;                     /* the slice runs to the next start code */
        while (end + 3 <= n && !(clip[end] == 0 && clip[end + 1] == 0 && clip[end + 2] == 1))
            end++;
        if (end + 3 > n)
            end = n;
        uint32_t ms = (uint32_t)au_count * 100;               /* 10 fps */
        CHECK(flv_video(&v, to_file, out, clip + au_start, end - au_start, ms) == 0, "video %zu", au_count);
        for (uint32_t t = ms; t < ms + 100; t += 40) {        /* the audio of that frame's slot */
            uint8_t u[320];
            for (unsigned s = 0; s < 320; s++)
                u[s] = flv_ulaw((int16_t)(8000 * sin(2 * M_PI * 400 * (t * 8 + s) / FLV_AUDIO_RATE)));
            CHECK(flv_audio(to_file, out, u, sizeof(u), t) == 0, "audio %u", t);
        }
        au_count++;
        au_start = end;
        i = end - 1;
    }
    fclose(out);
    CHECK(au_count == 3 && v.config_sent && v.started, "%zu access units, config %d", au_count, v.config_sent);
    return fails ? 1 : 0;
}
