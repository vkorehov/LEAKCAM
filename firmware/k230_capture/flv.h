/*
 * The leak clip's format: FLV with H.264 video and G.711 mu-law audio (8 kHz mono, 64 kbit/s), one
 * stream per camera, both carrying the microphone. FLV holds both natively, is written tag by tag
 * as the encoders deliver (no seeking back, no file), and ffmpeg / VLC play it.
 *
 * Written through a caller's sink, so the tags go straight into a chunked HTTP body; the sink may
 * be called several times per tag.
 */
#ifndef LEAKCAM_FLV_H
#define LEAKCAM_FLV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FLV_AUDIO_RATE 8000

typedef int (*flv_sink)(void *ctx, const void *p, size_t n);   /* 0 or -1 */

/* "FLV" header, audio + video, then PreviousTagSize0 */
int flv_header(flv_sink w, void *ctx);

/* one H.264 access unit as the encoder gives it (Annex-B, start codes). The first SPS and PPS
 * seen become the AVC sequence header; nothing is written before the first IDR. */
struct flv_video {
    uint8_t sps[64], pps[64];
    size_t sps_len, pps_len;
    bool config_sent, started;
};
int flv_video(struct flv_video *v, flv_sink w, void *ctx, const uint8_t *annexb, size_t n,
              uint32_t ms);

/* n mu-law bytes (n samples at FLV_AUDIO_RATE) starting at ms */
int flv_audio(flv_sink w, void *ctx, const uint8_t *ulaw, size_t n, uint32_t ms);

/* G.711 mu-law, the ITU/Sun reference: 16-bit PCM in, 8 bits out */
static inline uint8_t flv_ulaw(int16_t pcm)
{
    int v = pcm, sign = 0;
    if (v < 0) {
        v = -v;
        sign = 0x80;
    }
    if (v > 32635)
        v = 32635;
    v += 0x84;
    int exp = 7;
    for (int mask = 0x4000; !(v & mask) && exp > 0; mask >>= 1)
        exp--;
    return (uint8_t)~(sign | (exp << 4) | ((v >> (exp + 3)) & 0x0F));
}

#endif
