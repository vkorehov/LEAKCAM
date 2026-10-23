/* k230_linesplit.h: frames and console text on one wire, split exactly, in any chunking of the byte stream */
#include <stdio.h>
#include <string.h>

#include "k230_linesplit.h"

static char text[4096], frames[4096];
static unsigned ntext, nframes;

static void on_text(void *ctx, const char *p, unsigned n)
{
    (void)ctx;
    memcpy(text + ntext, p, n);
    ntext += n;
}

static void on_frame(void *ctx, const char *p, unsigned n)
{
    (void)ctx;
    memcpy(frames + nframes, p, n);
    nframes += n;
    frames[nframes++] = '|';
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void frame_of(char *out, const char *body)
{
    sprintf(out, "$%s*%02X", body, link_crc8(body, body + strlen(body)));
}

/* feed s in chunks of every size 1..len, each time from scratch, and compare */
static void run(const char *in, const char *want_text, const char *want_frames)
{
    unsigned len = (unsigned)strlen(in);
    for (unsigned chunk = 1; chunk <= len; chunk++) {
        struct linesplit s;
        linesplit_init(&s, on_text, on_frame, NULL);
        ntext = nframes = 0;
        for (unsigned i = 0; i < len; i += chunk)
            linesplit_feed(&s, in + i, len - i < chunk ? len - i : chunk);
        text[ntext] = frames[nframes] = 0;
        CHECK(!strcmp(text, want_text), "chunk %u: text [%s] want [%s]", chunk, text, want_text);
        CHECK(!strcmp(frames, want_frames), "chunk %u: frames [%s] want [%s]", chunk, frames, want_frames);
    }
}

int main(void)
{
    char a[64], b[64], in[512], want[512];
    frame_of(a, "3,READY");
    frame_of(b, "7,WAKE,rtc,1761000000,450,231,1650,4100");

    /* plain console text, no '$' */
    run("U-Boot SPL\r\nmsh />", "U-Boot SPL\r\nmsh />", "");
    /* one frame between console lines */
    sprintf(in, "boot ok\r\n%s\nmsh />", a);
    sprintf(want, "%s|", a);
    run(in, "boot ok\r\nmsh />", want);
    /* frames are reported whole, in order */
    sprintf(in, "%s\n%s\n", a, b);
    sprintf(want, "%s|%s|", a, b);
    run(in, "", want);
    /* console text with a '$' that is no frame (bad CRC, no '*', a shell prompt "$ ") stays text, byte for byte */
    run("price $5 today\n$3,READY*00\n$ ls\n", "price $5 today\n$3,READY*00\n$ ls\n", "");
    /* a frame right after text without a newline, and a '$' inside a candidate restarting it */
    sprintf(in, "msh />%s\n$$%s\n", a, a);
    sprintf(want, "%s|%s|", a, a);
    run(in, "msh />$$", want);       /* two stray '$' before the frame (a itself starts with '$') */
    /* an over-long '$' line is text, and the splitter recovers for the next frame */
    memset(in, 'x', 200);
    in[0] = '$';
    in[200] = 0;
    strcat(in, "\n");
    strcat(in, a);
    strcat(in, "\n");
    strcpy(want, in);
    want[201] = 0;                      /* the long line and its newline */
    char wf[80];
    sprintf(wf, "%s|", a);
    run(in, want, wf);

    if (fails) {
        printf("linesplit: %d failures\n", fails);
        return 1;
    }
    printf("linesplit: frames and console text split exactly in every chunking (text, frames, '$' in text, bad CRC, "
           "restart, over-long line)\n");
    return 0;
}
