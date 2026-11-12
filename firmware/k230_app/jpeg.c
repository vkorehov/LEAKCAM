#include "jpeg.h"

#include <string.h>

/* APP1 "Exif": a big-endian TIFF, IFD0 -> Exif IFD with DateTimeOriginal (ASCII 20 at 56) and OffsetTimeOriginal
 * (ASCII 7 at 76); offsets count from the TIFF header */
static const uint8_t head[] = {
    0xFF, 0xE1, 0, 92,                            /* APP1, its length: everything after the marker */
    'E', 'x', 'i', 'f', 0, 0,
    'M', 'M', 0, 42, 0, 0, 0, 8,                  /* TIFF header, IFD0 at 8 */
    0, 1,                                         /* IFD0: 1 entry */
    0x87, 0x69, 0, 4, 0, 0, 0, 1, 0, 0, 0, 26,   /* ExifIFDPointer (LONG) -> 26 */
    0, 0, 0, 0,                                   /* no next IFD */
    0, 2,                                         /* Exif IFD at 26: 2 entries */
    0x90, 0x03, 0, 2, 0, 0, 0, 20, 0, 0, 0, 56,  /* DateTimeOriginal */
    0x90, 0x11, 0, 2, 0, 0, 0, 7, 0, 0, 0, 76,   /* OffsetTimeOriginal */
    0, 0, 0, 0,                                   /* no next IFD */
};
_Static_assert(sizeof(head) - 2 + 20 + 8 == 92, "APP1 length");

size_t jpeg_exif(time_t taken, uint8_t *dst, size_t cap)
{
    struct tm tm;
    char when[20];
    size_t n = sizeof(head) + 20 + 8;
    if (cap < n || !gmtime_r(&taken, &tm) || strftime(when, sizeof(when), "%Y:%m:%d %H:%M:%S", &tm) != 19)
        return 0;
    memcpy(dst, head, sizeof(head));
    memcpy(dst + sizeof(head), when, 20);         /* with its NUL */
    memcpy(dst + sizeof(head) + 20, "+00:00\0", 8);   /* UTC; 7 + one pad byte */
    return n;
}
