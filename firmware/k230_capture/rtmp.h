/*
 * Live publish of one camera over RTMP: rtmp://<host>:<port>/<app>/<stream>, H.265 video plus
 * Opus mono audio (the microphone), both stamped in ms from the start, as Enhanced RTMP
 * (veovera.org E-RTMP v2): video FourCC "hvc1" with an HEVC decoder configuration record, audio
 * FourCC "Opus" with an RFC 7845 ID header, then coded frames. The ingest must support E-RTMP v2
 * (the server runs MediaMTX; FFmpeg 7.1+ in the host test). Simple handshake, no encryption, no
 * reconnect: a clip is 5 s.
 *
 * Not thread-safe: the video and audio threads share one struct rtmp under the caller's lock.
 */
#ifndef LEAKCAM_RTMP_H
#define LEAKCAM_RTMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


struct rtmp {
    int fd;
    uint32_t in_chunk, stream_id;
    uint8_t out[4096 + 64];         /* send buffer: whole chunks go out in one send() */
    size_t out_len;
    uint32_t msg_left, chunk_left;  /* the message being written */
    uint8_t msg_csid;
    uint8_t vps[96], sps[96], pps[96];   /* H.265 parameter sets for the sequence header */
    size_t vps_len, sps_len, pps_len;
    bool config_sent, started;
};

/* TCP connect, handshake, connect/createStream/publish; 0 or -1 (logged) */
int rtmp_publish(struct rtmp *r, const char *host, const char *port, const char *app,
                 const char *stream, int timeout_s);
/* one H.265 access unit as the encoder gives it (Annex-B). The first VPS, SPS and PPS seen
 * become the sequence header; nothing is sent before the first IRAP (IDR) picture. */
int rtmp_video(struct rtmp *r, const uint8_t *annexb, size_t n, uint32_t ms);
/* the Opus sequence start (RFC 7845 ID header), once before the first packet: mono, the
 * encoder's lookahead as pre-skip (OPUS_GET_LOOKAHEAD, in 48 kHz samples), its input rate */
int rtmp_opus_header(struct rtmp *r, unsigned pre_skip, unsigned input_rate);
/* one Opus packet (opus_encode output) starting at ms */
int rtmp_audio(struct rtmp *r, const uint8_t *packet, size_t n, uint32_t ms);
void rtmp_close(struct rtmp *r);

#endif
