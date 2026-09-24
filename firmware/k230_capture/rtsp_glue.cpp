/*
 * C wrapper around KdRtspServer (src/rtsmart/mpp/middleware/src/rtsp_server/include/rtsp_server.h).
 * Usage in the SDK: examples/mpp/sample_rtspserver/main.cpp and
 * examples/ai/triple_camera_ai_rtsp/src/encoded_rtsp_server.cc.
 */
#include "rtsp_glue.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "rtsp_server.h"

namespace {

class PlayHook : public IOnClientEvent {
  public:
    rtsp_play_cb cb = nullptr;
    void *user = nullptr;
    void OnClientPlay(const std::string &session, unsigned, size_t count) override {
        if (cb)
            cb(session.c_str(), count, user);
    }
};

KdRtspServer *g_srv;
PlayHook g_hook;

} // namespace

extern "C" int rtsp_glue_init(int port, rtsp_play_cb on_play, void *user)
{
    g_srv = new KdRtspServer();
    g_hook.cb = on_play;
    g_hook.user = user;
    return g_srv->Init(port, nullptr, &g_hook);
}

extern "C" int rtsp_glue_add_session(const char *name, enum rtsp_codec codec)
{
    SessionAttr a;
    a.with_video = true;
    a.with_audio = false;
    a.with_audio_backchannel = false;
    a.video_type = codec == RTSP_H265 ? VideoType::kVideoTypeH265
                 : codec == RTSP_MJPEG ? VideoType::kVideoTypeMjpeg
                                       : VideoType::kVideoTypeH264;
    return g_srv->CreateSession(name, a);
}

extern "C" void rtsp_glue_start(void) { g_srv->Start(); }

extern "C" int rtsp_glue_send(const char *name, const uint8_t *data, size_t size, uint64_t pts)
{
    return g_srv->SendVideoData(name, data, size, pts);
}

extern "C" size_t rtsp_glue_clients(const char *name) { return g_srv->GetClientCount(name); }

extern "C" void rtsp_glue_print_url(const char *name)
{
    char *url = g_srv->GetRtspUrl(name);   /* strdup() of live555 rtspURL(): caller frees */
    printf("rtsp: %s\n", url ? url : "(no url yet)");
    free(url);
}

extern "C" void rtsp_glue_stop(void)
{
    if (!g_srv)
        return;
    g_srv->Stop();
    g_srv->DeInit();
    delete g_srv;
    g_srv = nullptr;
}
