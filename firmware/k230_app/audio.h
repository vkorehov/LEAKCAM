/*
 * The microphone (U13 on the inner codec's left input) as Opus packets for the videos (user 2026-11-13: "we need to
 * stream also audio"; DESIGN.md 5.7). Proven in tests/test012 with leakcam_stream: 16 kHz mono, PGA 30 + ALC 24 dB,
 * Opus 24 kbit/s in 40 ms packets.
 */
#ifndef LEAKCAM_AUDIO_H
#define LEAKCAM_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#define AUDIO_RATE  16000
#define AUDIO_MS    40                  /* one AI frame = one Opus packet */

/* a packet: Opus, AUDIO_MS long, us = its first sample's time on the cameras' clock (the frames' pts) */
typedef void (*audio_fn)(const uint8_t *pkt, size_t n, uint64_t us, void *ctx);

/* the codec, its gains, Opus and the encoding thread, once (after the cameras' VB is up: the AI's frames come from
 * a VB pool). The codec's first ~2 s (its input capacitor charging) are not sent. 0 or -1 (logged). */
int audio_open(void);

/* all of it stopped and freed, before the cameras close (their VB exit fails while the AI's pool exists) */
void audio_close(void);

/* where the packets go from now on; NULL: dropped */
void audio_sink(audio_fn fn, void *ctx);

#endif
