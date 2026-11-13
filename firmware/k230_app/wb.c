/*
 * The cameras' white balance under the LEDs, kept for the next shot (wb.h; DESIGN.md 5.3.21; user 2026-11-13: "can
 * you persist settings so it find faster next time?", "do it, reverse engineer").
 *
 * The ISP library's automatic white balance (closed: libisp.a, awb_ctrl.o) sets three things per camera, each through
 * a global function called from other objects, so the linker's --wrap puts ours in between (skill
 * k230-vicap-isp-patch 3). Found by disassembly (cameric_isp_wb.o, cameric_isp_ccm.o, 2026-11-13); a0 = the CamerIc
 * handle of one ISP (one per camera), a1 = uint16 values:
 *   CamerIcIspWbSetGains                 4: red, green-red, green-blue, blue (1024 = 1.0 as read on the board)
 *   CamerIcIspSetCrossTalkCoefficients   9: the colour-correction matrix of the light the AWB found
 *   CamerIcIspSetCrossTalkOffset         3: its offsets
 * The gains alone are not the state: with the LED gains and the room's matrix the J4 camera came out warm (red
 * 1.40 x blue).
 *
 * When the LEDs go on for a shot the AWB still holds the room's light and needs frames to follow (the camera the LEDs
 * dominate came out blue at the 2nd-3rd frame, red 0.38-0.75 x blue), and it runs only every 3rd-4th frame (4-5 calls
 * in a 16-frame shot, 0-1 in a 3-frame one). So:
 *   - learn: with the LEDs on for LEARN gain calls in a row and the gains steady, the three sets are that camera's LED
 *     white balance, kept in /sdcard/leakcam/wb (written by wb_keep(), from the job's thread, when the gains moved by
 *     3 %);
 *   - hold: wb_light(true) sets each camera's kept LED white balance at once, and the AWB's first HOLD rounds after it
 *     get it in place of their own; a shot is over within them, a session (focus, live, a video) goes on with the
 *     AWB's own, settled by then. wb_light(false) puts the room's back (the AWB's last before the LEDs).
 * A camera without a kept one is not held: wb_known() false, the caller gives the AWB frames to settle instead (that
 * longer first shot teaches it).
 * The cameras are told apart by their handles in the order they first set gains (dev 0 first).
 */
#include "wb.h"

#include <stdio.h>
#include <string.h>

#include "leakcam.h"

#define WB_FILE "/sdcard/leakcam/wb"    /* per camera 16 numbers: gains 4, matrix 9, offsets 3, under the LEDs */
#define CAMS    2
#define HOLD    2                       /* the AWB's rounds held after the LEDs went on (one per 3-4 frames: a shot's) */
#define LEARN   4                       /* the AWB's rounds with the LEDs on before its result counts as settled (~14 frames) */
#define NG      4
#define NM      9
#define NO      3

struct wbset {
    uint16_t g[NG], m[NM], o[NO];
};

static void *handle[CAMS];
static struct wbset led[CAMS], saved[CAMS], last[CAMS], room[CAMS];
static bool have_m[CAMS], have_o[CAMS];     /* last[] holds a matrix / offsets (the AWB set them at least once) */
static bool led_m[CAMS], led_o[CAMS];       /* ... and led[] does (learnt with them, or read from the flash) */
static volatile bool lit;
static volatile unsigned calls[CAMS];       /* the AWB's gain calls since the LEDs went on */
static unsigned held[CAMS];

int __real_CamerIcIspWbSetGains(void *h, const uint16_t *g);
int __real_CamerIcIspSetCrossTalkCoefficients(void *h, const uint16_t *m);
int __real_CamerIcIspSetCrossTalkOffset(void *h, const uint16_t *o);

static int cam_of(void *h, bool add)
{
    for (int i = 0; i < CAMS; i++) {
        if (handle[i] == h)
            return i;
        if (!handle[i] && add) {
            handle[i] = h;
            return i;
        }
    }
    return -1;
}

static bool near(const uint16_t *a, const uint16_t *b, int n, unsigned permille)
{
    for (int i = 0; i < n; i++) {
        unsigned d = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
        if (d * 1000u > (unsigned)b[i] * permille)
            return false;
    }
    return true;
}

static void apply(int c, const struct wbset *s, bool m, bool o)
{
    __real_CamerIcIspWbSetGains(handle[c], s->g);
    if (m)
        __real_CamerIcIspSetCrossTalkCoefficients(handle[c], s->m);
    if (o)
        __real_CamerIcIspSetCrossTalkOffset(handle[c], s->o);
}

int __wrap_CamerIcIspWbSetGains(void *h, const uint16_t *g)
{
    int c = h && g ? cam_of(h, true) : -1;
    if (c < 0)
        return __real_CamerIcIspWbSetGains(h, g);
    bool steady = near(g, last[c].g, NG, 10);
    memcpy(last[c].g, g, sizeof(last[c].g));
    if (lit) {
        unsigned n = ++calls[c];
        if (n <= HOLD && led[c].g[1]) {         /* the shot's frames: the kept LED white balance, not the room's */
            held[c]++;
            return __real_CamerIcIspWbSetGains(h, led[c].g);
        }
        if (n >= LEARN && g[1] && steady) {     /* settled under the LEDs: this camera's LED white balance */
            led[c] = last[c];
            led_m[c] = have_m[c];
            led_o[c] = have_o[c];
        }
    }
    return __real_CamerIcIspWbSetGains(h, g);
}

int __wrap_CamerIcIspSetCrossTalkCoefficients(void *h, const uint16_t *m)
{
    int c = h && m ? cam_of(h, false) : -1;
    if (c < 0)
        return __real_CamerIcIspSetCrossTalkCoefficients(h, m);
    memcpy(last[c].m, m, sizeof(last[c].m));
    have_m[c] = true;
    return __real_CamerIcIspSetCrossTalkCoefficients(h, lit && calls[c] < HOLD && led[c].g[1] && led_m[c] ? led[c].m : m);
}

int __wrap_CamerIcIspSetCrossTalkOffset(void *h, const uint16_t *o)
{
    int c = h && o ? cam_of(h, false) : -1;
    if (c < 0)
        return __real_CamerIcIspSetCrossTalkOffset(h, o);
    memcpy(last[c].o, o, sizeof(last[c].o));
    have_o[c] = true;
    return __real_CamerIcIspSetCrossTalkOffset(h, lit && calls[c] < HOLD && led[c].g[1] && led_o[c] ? led[c].o : o);
}

void wb_load(void)
{
    FILE *f = fopen(WB_FILE, "r");
    if (!f)
        return;
    for (int c = 0; c < CAMS; c++) {
        struct wbset s;
        uint16_t *v = s.g;                      /* g, m, o: 16 uint16 in a row */
        int n = 0;
        unsigned x;
        while (n < NG + NM + NO && fscanf(f, "%u", &x) == 1 && x < 65536)
            v[n++] = (uint16_t)x;
        if (n == NG + NM + NO && s.g[1]) {
            led[c] = s;
            led_m[c] = led_o[c] = true;
        }
    }
    fclose(f);
    memcpy(saved, led, sizeof(saved));
}

void wb_light(bool on)
{
    if (on && !lit)
        for (int c = 0; c < CAMS; c++) {
            calls[c] = held[c] = 0;
            room[c] = last[c];
            if (handle[c] && led[c].g[1])       /* at once: a shot is over before the AWB's first round */
                apply(c, &led[c], led_m[c], led_o[c]);
        }
    if (!on && lit) {
        for (int c = 0; c < CAMS; c++)          /* back to the room's, until the AWB's next round */
            if (handle[c] && led[c].g[1] && room[c].g[1] && calls[c] <= HOLD)
                apply(c, &room[c], have_m[c], have_o[c]);
        logmsg("wb: LEDs off: cam0 %u rounds (%u held) LED gains r %u b %u, cam1 %u rounds (%u held) r %u b %u; the "
               "AWB's last r/b %u/%u %u/%u", calls[0], held[0], led[0].g[0], led[0].g[3], calls[1], held[1],
               led[1].g[0], led[1].g[3], last[0].g[0], last[0].g[3], last[1].g[0], last[1].g[3]);
    }
    lit = on;
}

bool wb_known(void)
{
    for (int c = 0; c < CAMS; c++)
        if (handle[c] && !led[c].g[1])
            return false;
    return handle[0] != NULL;
}

void wb_keep(void)
{
    bool moved = false;
    for (int c = 0; c < CAMS; c++)
        if (led[c].g[1] && (!saved[c].g[1] || !near(led[c].g, saved[c].g, NG, 30)))
            moved = true;
    if (!moved)
        return;
    FILE *f = fopen(WB_FILE, "w");
    if (!f)
        return;
    for (int c = 0; c < CAMS; c++) {
        const uint16_t *v = led[c].g;
        for (int i = 0; i < NG + NM + NO; i++)
            fprintf(f, "%u%c", v[i], i == NG + NM + NO - 1 ? '\n' : ' ');
    }
    fclose(f);
    memcpy(saved, led, sizeof(saved));
    logmsg("wb: LED white balance kept: cam0 gains r %u b %u, cam1 r %u b %u", led[0].g[0], led[0].g[3], led[1].g[0],
           led[1].g[3]);
}
