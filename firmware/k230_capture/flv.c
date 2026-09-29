#include "flv.h"

#include <string.h>

#define TAG_AUDIO 8
#define TAG_VIDEO 9

static void be24(uint8_t *p, uint32_t v) { p[0] = v >> 16; p[1] = v >> 8; p[2] = v; }
static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; be24(p + 1, v); }

int flv_header(flv_sink w, void *ctx)
{
    static const uint8_t h[13] = { 'F', 'L', 'V', 1, 0x05, 0, 0, 0, 9, 0, 0, 0, 0 };
    return w(ctx, h, sizeof(h));
}

/* tag header: type, 24-bit data size, 24-bit ms + 8-bit ms extension, stream id 0 */
static int tag_start(flv_sink w, void *ctx, uint8_t type, uint32_t size, uint32_t ms)
{
    uint8_t h[11] = { type };
    be24(h + 1, size);
    be24(h + 4, ms & 0xFFFFFF);
    h[7] = (uint8_t)(ms >> 24);
    return w(ctx, h, sizeof(h));
}

static int tag_end(flv_sink w, void *ctx, uint32_t size)
{
    uint8_t p[4];
    be32(p, 11 + size);                         /* PreviousTagSize */
    return w(ctx, p, sizeof(p));
}

int flv_audio(flv_sink w, void *ctx, const uint8_t *ulaw, size_t n, uint32_t ms)
{
    /* SoundFormat 8 (G.711 mu-law), rate bits 0 (implied 8 kHz), 16-bit, mono: as ffmpeg's flvenc */
    const uint8_t flags = (8 << 4) | (0 << 2) | (1 << 1) | 0;
    uint32_t size = 1 + (uint32_t)n;
    if (tag_start(w, ctx, TAG_AUDIO, size, ms) || w(ctx, &flags, 1) || w(ctx, ulaw, n))
        return -1;
    return tag_end(w, ctx, size);
}

/* next NAL unit after *pos (start code skipped); its length through *len, NULL at the end */
static const uint8_t *next_nal(const uint8_t *b, size_t n, size_t *pos, size_t *len)
{
    size_t i = *pos;
    while (i + 3 <= n && !(b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1))
        i++;
    if (i + 3 > n)
        return NULL;
    size_t start = i + 3, j = start;
    while (j + 3 <= n && !(b[j] == 0 && b[j + 1] == 0 && (b[j + 2] == 1 || (b[j + 2] == 0 && j + 3 < n && b[j + 3] == 1))))
        j++;
    if (j + 3 > n)
        j = n;
    *pos = j;
    *len = j - start;
    return b + start;
}

static int sequence_header(struct flv_video *v, flv_sink w, void *ctx, uint32_t ms)
{
    /* AVCDecoderConfigurationRecord: version, profile, compatibility, level, 4-byte NAL lengths,
     * one SPS, one PPS */
    uint8_t h[5 + 11] = { 0x17, 0, 0, 0, 0, 1, v->sps[1], v->sps[2], v->sps[3], 0xFF, 0xE1 };
    uint8_t sl[2] = { (uint8_t)(v->sps_len >> 8), (uint8_t)v->sps_len };
    uint8_t pl[3] = { 1, (uint8_t)(v->pps_len >> 8), (uint8_t)v->pps_len };
    uint32_t size = 5 + 6 + 2 + (uint32_t)v->sps_len + 3 + (uint32_t)v->pps_len;
    if (tag_start(w, ctx, TAG_VIDEO, size, ms) || w(ctx, h, 11) || w(ctx, sl, 2) ||
        w(ctx, v->sps, v->sps_len) || w(ctx, pl, 3) || w(ctx, v->pps, v->pps_len))
        return -1;
    v->config_sent = true;
    return tag_end(w, ctx, size);
}

/* the NAL units that go into a picture tag: not SPS (7), PPS (8) or access unit delimiters (9) */
static bool picture_nal(uint8_t type) { return type != 7 && type != 8 && type != 9; }

int flv_video(struct flv_video *v, flv_sink w, void *ctx, const uint8_t *b, size_t n, uint32_t ms)
{
    size_t pos = 0, len, payload = 0;
    bool key = false;
    const uint8_t *nal;
    while ((nal = next_nal(b, n, &pos, &len))) {
        if (!len)
            continue;
        uint8_t type = nal[0] & 0x1F;
        if (type == 7 && len <= sizeof(v->sps) && !v->sps_len)
            memcpy(v->sps, nal, v->sps_len = len);
        else if (type == 8 && len <= sizeof(v->pps) && !v->pps_len)
            memcpy(v->pps, nal, v->pps_len = len);
        if (type == 5)
            key = true;
        if (picture_nal(type))
            payload += 4 + len;
    }
    if (!v->config_sent && v->sps_len && v->pps_len && sequence_header(v, w, ctx, ms))
        return -1;
    if (key && v->config_sent)
        v->started = true;
    if (!v->started || !payload)
        return 0;                               /* the clip starts on its first IDR */

    uint8_t h[5] = { key ? 0x17 : 0x27, 1, 0, 0, 0 };   /* AVC NALU, composition time 0 */
    uint32_t size = 5 + (uint32_t)payload;
    if (tag_start(w, ctx, TAG_VIDEO, size, ms) || w(ctx, h, sizeof(h)))
        return -1;
    pos = 0;
    while ((nal = next_nal(b, n, &pos, &len))) {
        if (!len || !picture_nal(nal[0] & 0x1F))
            continue;
        uint8_t l[4];
        be32(l, (uint32_t)len);
        if (w(ctx, l, 4) || w(ctx, nal, len))
            return -1;
    }
    return tag_end(w, ctx, size);
}
