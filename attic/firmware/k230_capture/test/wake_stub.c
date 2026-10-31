/* Host stand-ins for the cameras and LEDs of leakcam_wake: every camera "sees" the scene named
 * by LEAKCAM_TEST_SCENE, "dry" (floor and furniture) or "wet" (the same with a dark puddle). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cap.h"
#include "led.h"

const unsigned led_channel[2] = { 1, 0 };
const char *const led_name[2] = { "white", "ir" };

int led_set(bool on, const unsigned percent[2], unsigned pwm_dev)
{
    (void)on; (void)percent; (void)pwm_dev;
    return 0;
}

int cap_open_all(struct cap_cam *cams, int n, unsigned width, unsigned height)
{
    for (int i = 0; i < n; i++) {
        cams[i].slot = cams[i].node;
        cams[i].width = cams[i].stride = width;
        cams[i].height = height;
    }
    return 0;
}

int cap_grab_all(struct cap_cam *cams, int n, unsigned settle, int timeout_ms)
{
    (void)settle; (void)timeout_ms;
    const char *s = getenv("LEAKCAM_TEST_SCENE");
    int wet = s && !strcmp(s, "wet");
    for (int i = 0; i < n; i++) {
        unsigned w = cams[i].width, h = cams[i].height;
        uint8_t *img = malloc((size_t)w * h);
        if (!img)
            return -1;
        for (unsigned y = 0; y < h; y++)
            for (unsigned x = 0; x < w; x++) {
                float dx = x - w / 2.0f, dy = y - h / 2.0f;
                float v = dx * dx + dy * dy < 790.0f * 790.0f ? 60.0f + 80.0f * y / h : 1.0f;
                if ((x / 160 + y / 120) % 5 == 0)
                    v += 30;
                if (wet && (x - 700.0f) * (x - 700.0f) + (y - 500.0f) * (y - 500.0f) < 60.0f * 60.0f)
                    v -= 40;
                img[y * w + x] = (uint8_t)lroundf(v);
            }
        cams[i].luma = img;
    }
    return 0;
}

void cap_close_all(struct cap_cam *cams, int n)
{
    for (int i = 0; i < n; i++) {
        free(cams[i].luma);
        cams[i].luma = NULL;
    }
}
