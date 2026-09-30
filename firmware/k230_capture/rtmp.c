#include "rtmp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "netclient.h"

#define HS_SIZE     1536
#define OUT_CHUNK   4096            /* announced with Set Chunk Size before anything else */
#define MSG_CHUNK   1               /* protocol control: Set Chunk Size */
#define MSG_AUDIO   8
#define MSG_VIDEO   9
#define MSG_CMD     20              /* AMF0 command */
#define CSID_CTRL   2
#define CSID_CMD    3
#define CSID_AUDIO  4
#define CSID_VIDEO  6
#define IN_MAX      4096            /* setup replies are small; the rest of a longer one is dropped */

static void be16(uint8_t *p, uint32_t v) { p[0] = v >> 8; p[1] = v; }
static void be24(uint8_t *p, uint32_t v) { p[0] = v >> 16; be16(p + 1, v); }
static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; be24(p + 1, v); }
static uint32_t rd24(const uint8_t *p) { return (uint32_t)p[0] << 16 | p[1] << 8 | p[2]; }
static uint32_t rd32(const uint8_t *p) { return rd24(p) << 8 | p[3]; }

static int recv_all(int fd, void *p, size_t n)
{
    uint8_t *b = p;
    while (n) {
        ssize_t k = recv(fd, b, n, 0);
        if (k <= 0)
            return -1;
        b += k;
        n -= (size_t)k;
    }
    return 0;
}

/* ---------------- sending: messages cut into chunks, through the send buffer ---------------- */

static int flush(struct rtmp *r)
{
    int rc = r->out_len ? net_send_all(r->fd, r->out, r->out_len) : 0;
    r->out_len = 0;
    return rc;
}

static int put(struct rtmp *r, const void *p, size_t n)
{
    const uint8_t *b = p;
    while (n) {
        if (r->out_len == sizeof(r->out) && flush(r))
            return -1;
        size_t k = sizeof(r->out) - r->out_len;
        if (k > n)
            k = n;
        memcpy(r->out + r->out_len, b, k);
        r->out_len += k;
        b += k;
        n -= k;
    }
    return 0;
}

/* type 0 chunk header: absolute timestamp, length, type, message stream id (little endian) */
static int msg_begin(struct rtmp *r, uint8_t csid, uint8_t type, uint32_t len, uint32_t ms, uint32_t msid)
{
    uint8_t h[12] = { csid };
    be24(h + 1, ms < 0xFFFFFF ? ms : 0xFFFFFF);  /* a 5 s clip never needs the extended stamp */
    be24(h + 4, len);
    h[7] = type;
    h[8] = (uint8_t)msid;
    h[9] = (uint8_t)(msid >> 8);
    h[10] = (uint8_t)(msid >> 16);
    h[11] = (uint8_t)(msid >> 24);
    r->msg_csid = csid;
    r->msg_left = len;
    r->chunk_left = len < OUT_CHUNK ? len : OUT_CHUNK;
    return put(r, h, sizeof(h));
}

/* message body; a type 3 header starts every further chunk */
static int msg_put(struct rtmp *r, const void *p, size_t n)
{
    const uint8_t *b = p;
    while (n) {
        if (!r->chunk_left) {
            uint8_t h = 0xC0 | r->msg_csid;
            if (put(r, &h, 1))
                return -1;
            r->chunk_left = r->msg_left < OUT_CHUNK ? r->msg_left : OUT_CHUNK;
        }
        size_t k = n < r->chunk_left ? n : r->chunk_left;
        if (put(r, b, k))
            return -1;
        r->chunk_left -= (uint32_t)k;
        r->msg_left -= (uint32_t)k;
        b += k;
        n -= k;
    }
    return 0;
}

/* ---------------- AMF0, as much as the three setup commands need ---------------- */

static uint8_t *amf_str(uint8_t *p, const char *s)
{
    size_t n = strlen(s);
    *p++ = 0x02;
    be16(p, (uint32_t)n);
    memcpy(p + 2, s, n);
    return p + 2 + n;
}

static uint8_t *amf_num(uint8_t *p, double d)
{
    uint64_t u;
    memcpy(&u, &d, 8);
    *p++ = 0x00;
    for (int i = 7; i >= 0; i--)
        *p++ = (uint8_t)(u >> (8 * i));
    return p;
}

static uint8_t *amf_key(uint8_t *p, const char *k)
{
    size_t n = strlen(k);
    be16(p, (uint32_t)n);
    memcpy(p + 2, k, n);
    return p + 2 + n;
}

static int send_cmd(struct rtmp *r, const uint8_t *body, size_t n, uint32_t msid)
{
    if (msg_begin(r, CSID_CMD, MSG_CMD, (uint32_t)n, 0, msid) || msg_put(r, body, n))
        return -1;
    return flush(r);
}

/* the number after the command name: the transaction id */
static double amf_txn(const uint8_t *p, size_t n)
{
    if (n < 3 || p[0] != 0x02)
        return -1;
    size_t at = 3 + ((size_t)p[1] << 8 | p[2]);
    if (at + 9 > n || p[at] != 0x00)
        return -1;
    uint64_t u = 0;
    for (int i = 1; i <= 8; i++)
        u = u << 8 | p[at + i];
    double d;
    memcpy(&d, &u, 8);
    return d;
}

static bool has(const uint8_t *p, size_t n, const char *s)
{
    size_t k = strlen(s);
    for (size_t i = 0; i + k <= n; i++)
        if (!memcmp(p + i, s, k))
            return true;
    return false;
}

/* ---------------- receiving: chunks back into messages ---------------- */

struct in_stream {
    uint32_t len, got;
    uint8_t type;
    uint8_t buf[IN_MAX];
};

/* the next whole message from the server; Set Chunk Size is applied here */
static int read_msg(struct rtmp *r, struct in_stream st[8], uint8_t *type, const uint8_t **body, size_t *len)
{
    while (1) {
        uint8_t b, h[11];
        if (recv_all(r->fd, &b, 1))
            return -1;
        uint32_t csid = b & 0x3F, fmt = b >> 6;
        if (csid < 2) {                              /* 2- and 3-byte basic headers */
            uint8_t x[2] = { 0, 0 };
            if (recv_all(r->fd, x, csid + 1))
                return -1;
            csid = 64 + x[0] + 256u * x[1];
        }
        struct in_stream *s = &st[csid % 8];
        static const uint8_t hlen[4] = { 11, 7, 3, 0 };
        if (hlen[fmt] && recv_all(r->fd, h, hlen[fmt]))
            return -1;
        if (hlen[fmt] && rd24(h) == 0xFFFFFF && recv_all(r->fd, h + 7, 4))   /* extended stamp */
            return -1;
        if (fmt <= 1) {
            s->len = rd24(h + 3);
            s->type = h[6];
            s->got = 0;
        }
        uint32_t k = s->len - s->got;
        if (k > r->in_chunk)
            k = r->in_chunk;
        for (uint32_t i = 0; i < k; i++) {           /* bodies over IN_MAX keep their head only */
            if (recv_all(r->fd, &b, 1))
                return -1;
            if (s->got + i < IN_MAX)
                s->buf[s->got + i] = b;
        }
        s->got += k;
        if (s->got < s->len)
            continue;
        s->got = 0;
        if (s->type == MSG_CHUNK && s->len >= 4)
            r->in_chunk = rd32(s->buf) & 0x7FFFFFFF;
        *type = s->type;
        *body = s->buf;
        *len = s->len < IN_MAX ? s->len : IN_MAX;
        return 0;
    }
}

/* the reply to command txn (or, txn < 0, an onStatus with want in it); _error fails */
static int wait_reply(struct rtmp *r, struct in_stream st[8], double txn, const char *want,
                      const uint8_t **body, size_t *len)
{
    uint8_t type;
    while (!read_msg(r, st, &type, body, len)) {
        if (type != MSG_CMD)
            continue;
        if (has(*body, *len > 16 ? 16 : *len, "_error") || has(*body, *len, "NetStream.Publish.BadName"))
            return -1;
        if (txn >= 0 && has(*body, *len > 16 ? 16 : *len, "_result") && amf_txn(*body, *len) == txn)
            return 0;
        if (txn < 0 && has(*body, *len, want))
            return 0;
    }
    return -1;
}

static int handshake(int fd)
{
    static uint8_t c[1 + HS_SIZE], s[1 + 2 * HS_SIZE];
    c[0] = 3;                                         /* plain RTMP */
    memset(c + 1, 0, 8);                              /* time 0, zero */
    srand((unsigned)time(NULL));
    for (int i = 9; i < 1 + HS_SIZE; i++)
        c[i] = (uint8_t)rand();
    if (net_send_all(fd, c, sizeof(c)) || recv_all(fd, s, sizeof(s)))
        return -1;
    return net_send_all(fd, s + 1, HS_SIZE);          /* C2 = S1 echoed */
}

int rtmp_publish(struct rtmp *r, const char *host, const char *port, const char *app,
                 const char *stream, int timeout_s)
{
    static struct in_stream st[8];
    memset(r, 0, sizeof(*r));
    r->in_chunk = 128;
    r->fd = net_connect(host, port, timeout_s);
    if (r->fd < 0 || handshake(r->fd)) {
        fprintf(stderr, "rtmp: %s:%s: no handshake\n", host, port);
        return -1;
    }

    uint8_t c[4];                                     /* our chunks from now on: 4096 */
    be32(c, OUT_CHUNK);
    if (msg_begin(r, CSID_CTRL, MSG_CHUNK, 4, 0, 0) || put(r, c, 4) || flush(r))
        return -1;

    uint8_t cmd[512], *p = amf_str(cmd, "connect");
    char url[160];
    snprintf(url, sizeof(url), "rtmp://%s:%s/%s", host, port, app);
    p = amf_num(p, 1);
    *p++ = 0x03;                                      /* command object */
    p = amf_key(p, "app");      p = amf_str(p, app);
    p = amf_key(p, "type");     p = amf_str(p, "nonprivate");
    p = amf_key(p, "flashVer"); p = amf_str(p, "FMLE/3.0 (compatible; LEAKCAM)");
    p = amf_key(p, "tcUrl");    p = amf_str(p, url);
    p = amf_key(p, "fourCcList");                     /* Enhanced RTMP: we send H.265 and Opus */
    *p++ = 0x0A;                                      /* strict array */
    be32(p, 2);
    p = amf_str(amf_str(p + 4, "hvc1"), "Opus");
    *p++ = 0; *p++ = 0; *p++ = 9;                     /* object end */
    const uint8_t *body;
    size_t len;
    if (send_cmd(r, cmd, (size_t)(p - cmd), 0) || wait_reply(r, st, 1, NULL, &body, &len)) {
        fprintf(stderr, "rtmp: connect to %s refused\n", url);
        return -1;
    }

    p = amf_num(amf_str(cmd, "createStream"), 2);
    *p++ = 0x05;                                      /* null */
    if (send_cmd(r, cmd, (size_t)(p - cmd), 0) || wait_reply(r, st, 2, NULL, &body, &len) || len < 10 ||
        body[len - 9] != 0x00) {
        fprintf(stderr, "rtmp: createStream failed\n");
        return -1;
    }
    uint64_t u = 0;                                   /* the stream id: the reply's last number */
    for (size_t i = len - 8; i < len; i++)
        u = u << 8 | body[i];
    double sid;
    memcpy(&sid, &u, 8);
    r->stream_id = (uint32_t)sid;

    p = amf_num(amf_str(cmd, "publish"), 3);
    *p++ = 0x05;
    p = amf_str(amf_str(p, stream), "live");
    if (send_cmd(r, cmd, (size_t)(p - cmd), r->stream_id) ||
        wait_reply(r, st, -1, "NetStream.Publish.Start", &body, &len)) {
        fprintf(stderr, "rtmp: publish %s/%s refused\n", url, stream);
        return -1;
    }
    return 0;
}

/* ---------------- media ---------------- */

/* E-RTMP v2 audio: SoundFormat 9 (ExHeader) << 4 | AudioPacketType, then the FourCC */
#define ERTMP_AUDIO_SEQUENCE_START 0
#define ERTMP_AUDIO_CODED_FRAMES   1
static int audio_msg(struct rtmp *r, uint8_t packet, const uint8_t *body, size_t n, uint32_t ms)
{
    uint8_t h[5] = { (uint8_t)(9 << 4 | packet), 'O', 'p', 'u', 's' };
    if (r->fd < 0 || msg_begin(r, CSID_AUDIO, MSG_AUDIO, 5 + (uint32_t)n, ms, r->stream_id) ||
        msg_put(r, h, sizeof(h)) || msg_put(r, body, n))
        return -1;
    return flush(r);
}

int rtmp_opus_header(struct rtmp *r, unsigned pre_skip, unsigned input_rate)
{
    uint8_t h[19] = { 'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, 1,    /* version 1, mono */
                      (uint8_t)pre_skip, (uint8_t)(pre_skip >> 8),
                      (uint8_t)input_rate, (uint8_t)(input_rate >> 8), (uint8_t)(input_rate >> 16),
                      (uint8_t)(input_rate >> 24), 0, 0, 0 };        /* gain 0, mapping family 0 */
    return audio_msg(r, ERTMP_AUDIO_SEQUENCE_START, h, sizeof(h), 0);
}

int rtmp_audio(struct rtmp *r, const uint8_t *packet, size_t n, uint32_t ms)
{
    return audio_msg(r, ERTMP_AUDIO_CODED_FRAMES, packet, n, ms);
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

/* H.265 NAL unit types used here (the type is bits 1..6 of the first header byte) */
#define HEVC_VPS 32
#define HEVC_SPS 33
#define HEVC_PPS 34
#define HEVC_AUD 35
static uint8_t nal_type(const uint8_t *nal) { return (nal[0] >> 1) & 0x3F; }
static bool irap(uint8_t type) { return type >= 16 && type <= 23; }        /* IDR, CRA, BLA */
static bool picture_nal(uint8_t type) { return type < HEVC_VPS || type > HEVC_AUD; }

/* Enhanced RTMP video: IsExHeader | FrameType << 4 | PacketType, then the FourCC */
#define ERTMP_SEQUENCE_START 0
#define ERTMP_CODED_FRAMES_X 3               /* coded frames without a composition time */
static int video_head(struct rtmp *r, bool key, uint8_t packet, uint32_t size, uint32_t ms)
{
    uint8_t h[5] = { (uint8_t)(0x80 | (key ? 1 : 2) << 4 | packet), 'h', 'v', 'c', '1' };
    if (msg_begin(r, CSID_VIDEO, MSG_VIDEO, 5 + size, ms, r->stream_id))
        return -1;
    return msg_put(r, h, sizeof(h));
}

/* the first 13 bytes of the SPS payload with emulation prevention (00 00 03) removed: the
 * sub-layer byte and general profile_tier_level, all byte-aligned (ITU-T H.265 7.3.2.2) */
static int sps_head(const uint8_t *sps, size_t n, uint8_t out[13])
{
    size_t k = 0, zeros = 0;
    for (size_t i = 2; i < n && k < 13; i++) {          /* after the 2-byte NAL header */
        if (zeros >= 2 && sps[i] == 3) {
            zeros = 0;
            continue;
        }
        zeros = sps[i] ? 0 : zeros + 1;
        out[k++] = sps[i];
    }
    return k == 13 ? 0 : -1;
}

static int sequence_header(struct rtmp *r, uint32_t ms)
{
    /* HEVCDecoderConfigurationRecord (ISO/IEC 14496-15 8.3.3.1.2). Profile, tier, compatibility,
     * constraints and level are copied from the SPS; chroma 4:2:0 and 8 bits are what Main
     * profile (the K230 encoder's) means; 4-byte NAL lengths; one VPS, SPS and PPS */
    uint8_t p[13];
    if (sps_head(r->sps, r->sps_len, p))
        return -1;
    uint8_t layers = (uint8_t)(((p[0] >> 1) & 7) + 1), nested = p[0] & 1;
    uint8_t c[23] = { 1 };
    memcpy(c + 1, p + 1, 12);                           /* profile byte, 32 + 48 flag bits, level */
    c[13] = 0xF0;                                       /* min_spatial_segmentation_idc 0 */
    c[14] = 0x00;
    c[15] = 0xFC;                                       /* parallelismType 0 */
    c[16] = 0xFD;                                       /* chroma_format_idc 1 (4:2:0) */
    c[17] = 0xF8;                                       /* bit depth luma 8 */
    c[18] = 0xF8;                                       /* bit depth chroma 8 */
    c[19] = c[20] = 0;                                  /* avgFrameRate unknown */
    c[21] = (uint8_t)(layers << 3 | nested << 2 | 3);   /* lengthSizeMinusOne 3 */
    c[22] = 3;                                          /* arrays: VPS, SPS, PPS */
    const uint8_t *ps[3] = { r->vps, r->sps, r->pps };
    const size_t len[3] = { r->vps_len, r->sps_len, r->pps_len };
    uint32_t size = sizeof(c);
    for (int i = 0; i < 3; i++)
        size += 5 + (uint32_t)len[i];
    if (video_head(r, true, ERTMP_SEQUENCE_START, size, ms) || msg_put(r, c, sizeof(c)))
        return -1;
    for (int i = 0; i < 3; i++) {
        uint8_t a[5] = { (uint8_t)(0x80 | (HEVC_VPS + i)), 0, 1, (uint8_t)(len[i] >> 8), (uint8_t)len[i] };
        if (msg_put(r, a, sizeof(a)) || msg_put(r, ps[i], len[i]))
            return -1;
    }
    r->config_sent = true;
    return flush(r);
}

static void keep(uint8_t *dst, size_t *dst_len, size_t cap, const uint8_t *nal, size_t len)
{
    if (!*dst_len && len <= cap)
        memcpy(dst, nal, *dst_len = len);
}

int rtmp_video(struct rtmp *r, const uint8_t *b, size_t n, uint32_t ms)
{
    size_t pos = 0, len, payload = 0;
    bool key = false;
    const uint8_t *nal;
    if (r->fd < 0)
        return -1;
    while ((nal = next_nal(b, n, &pos, &len))) {
        if (len < 2)
            continue;
        uint8_t type = nal_type(nal);
        if (type == HEVC_VPS)
            keep(r->vps, &r->vps_len, sizeof(r->vps), nal, len);
        else if (type == HEVC_SPS)
            keep(r->sps, &r->sps_len, sizeof(r->sps), nal, len);
        else if (type == HEVC_PPS)
            keep(r->pps, &r->pps_len, sizeof(r->pps), nal, len);
        if (irap(type))
            key = true;
        if (picture_nal(type))
            payload += 4 + len;
    }
    if (!r->config_sent && r->vps_len && r->sps_len && r->pps_len && sequence_header(r, ms))
        return -1;
    if (key && r->config_sent)
        r->started = true;
    if (!r->started || !payload)
        return 0;                                     /* the stream starts on its first IDR */

    if (video_head(r, key, ERTMP_CODED_FRAMES_X, (uint32_t)payload, ms))
        return -1;
    pos = 0;
    while ((nal = next_nal(b, n, &pos, &len))) {
        if (len < 2 || !picture_nal(nal_type(nal)))
            continue;
        uint8_t l[4];
        be32(l, (uint32_t)len);
        if (msg_put(r, l, 4) || msg_put(r, nal, len))
            return -1;
    }
    return flush(r);
}

/* deleteStream first, so the server ends the stream cleanly instead of on a dropped connection */
void rtmp_close(struct rtmp *r)
{
    if (r->fd >= 0) {
        uint8_t cmd[64], *p = amf_num(amf_str(cmd, "deleteStream"), 4);
        *p++ = 0x05;
        p = amf_num(p, r->stream_id);
        flush(r);
        if (r->stream_id)
            send_cmd(r, cmd, (size_t)(p - cmd), 0);
        close(r->fd);
    }
    r->fd = -1;
}
