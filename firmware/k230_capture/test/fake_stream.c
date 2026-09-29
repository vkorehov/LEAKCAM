/* Host stand-in for leakcam_stream's push mode: the same command line and the same upload per
 * camera (chunked POST /v1/video?cam=<N>), with a fixed fake H.264 body instead of the encoder. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "netclient.h"

int main(int argc, char **argv)
{
    char host[64], port[8];
    if (argc != 5 || strcmp(argv[1], "-p") || strcmp(argv[3], "-t") ||
        sscanf(argv[2], "%63[^:]:%7s", host, port) != 2)
        return 2;
    static const unsigned char nal[] = { 0, 0, 0, 1, 0x65, 0x88, 0x84, 0x00 };   /* an IDR start */
    for (int cam = 0; cam < 2; cam++) {
        char path[32], reply[128];
        snprintf(path, sizeof(path), "/v1/video?cam=%d", cam);
        int fd = net_connect(host, port, 5);
        if (fd < 0 || http_post(fd, host, path, "video/h264", -1) < 0)
            return 1;
        for (int i = 0; i < 100; i++)                  /* 800 bytes in 100 chunks */
            if (http_chunk(fd, nal, sizeof(nal)) < 0)
                return 1;
        if (http_chunk(fd, NULL, 0) < 0 || http_reply(fd, reply, sizeof(reply)) != 200 ||
            !strstr(reply, "\"bytes\":800"))
            return 1;
        close(fd);
    }
    printf("seconds=%s\n", argv[4]);
    return 0;
}
