/* Host stand-in for leakcam_stream's push mode: checks the command line leakcam_wake gives it and
 * appends it to FAKE_STREAM_LOG for test_wake.py. The RTMP publish itself is test_rtmp's. */
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    char host[64], port[8];
    if (argc != 5 || strcmp(argv[1], "-p") || strcmp(argv[3], "-t") ||
        sscanf(argv[2], "%63[^:]:%7s", host, port) != 2)
        return 2;
    FILE *f = fopen(FAKE_STREAM_LOG, "a");
    if (!f)
        return 1;
    fprintf(f, "%s %s\n", argv[2], argv[4]);
    fclose(f);
    return 0;
}
