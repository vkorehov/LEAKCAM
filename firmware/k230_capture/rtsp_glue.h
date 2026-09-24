/*
 * C wrapper around the SDK's C++ RTSP server (KdRtspServer, librtsp_server.a on live555).
 * Header: src/rtsmart/mpp/middleware/src/rtsp_server/include/rtsp_server.h, installed to
 * output/<defconfig>/rtsmart/mpp/middleware/include/rtsp_server.h by the middleware build.
 */
#ifndef LEAKCAM_RTSP_GLUE_H
#define LEAKCAM_RTSP_GLUE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum rtsp_codec { RTSP_H264 = 0, RTSP_H265 = 1, RTSP_MJPEG = 2 };

/* Called on the live555 event-loop thread when a client starts PLAY on a session.
 * Must not block (see IOnClientEvent in rtsp_server.h): only set a flag. */
typedef void (*rtsp_play_cb)(const char *session, size_t clients, void *user);

int  rtsp_glue_init(int port, rtsp_play_cb on_play, void *user);   /* KdRtspServer::Init */
int  rtsp_glue_add_session(const char *name, enum rtsp_codec codec); /* CreateSession, video only */
void rtsp_glue_start(void);                                        /* Start: spawns the event loop */
/* One encoder pack (Annex-B, start codes included, as kd_mpi_venc_get_stream returns it).
 * The server copies the data; the caller may munmap right after. pts: ms or us (auto-detected). */
int  rtsp_glue_send(const char *name, const uint8_t *data, size_t size, uint64_t pts);
size_t rtsp_glue_clients(const char *name);                        /* GetClientCount */
void rtsp_glue_print_url(const char *name);                        /* GetRtspUrl */
void rtsp_glue_stop(void);                                         /* Stop + DeInit */

#ifdef __cplusplus
}
#endif
#endif
