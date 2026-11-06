/*
 * /dev/ota: the K230 image's A/B update on SPI NAND (DESIGN.md 5.3.7, tests/test130). Shared by the kernel driver
 * (ota_nand.c) and the program that writes an update (k230_app).
 *
 *   ioctl(OTA_IOC_BEGIN, struct ota_begin *)  the inactive slot (the SPL's rule: the valid record with the higher
 *                                             version boots, A on a tie or with none), write and read positions at its
 *                                             start
 *   write(buf, n)                             the image, appended: each block checked (a bad one skipped, as the SPL
 *                                             skips it when it loads the slot) and erased before its first page
 *   ioctl(OTA_IOC_FLUSH)                      the last partial page written (0xFF after the image)
 *   read(buf, n)                              the slot from its start, bad blocks skipped: the read-back for the
 *                                             SHA-256 check
 *   ioctl(OTA_IOC_COMMIT, uint32_t *version)  the slot's ota_meta record (written last: until then the old slot boots)
 *   ioctl(OTA_IOC_RECORDS, struct ota_records *)  both records as the SPL reads them
 */
#ifndef LEAKCAM_OTA_NAND_H
#define LEAKCAM_OTA_NAND_H

#include <stdint.h>

#define OTA_IOC_BEGIN   0x4f01
#define OTA_IOC_FLUSH   0x4f02
#define OTA_IOC_COMMIT  0x4f03
#define OTA_IOC_RECORDS 0x4f04

#define OTA_META_MAGIC  0x4f544156u   /* 'OTAV', the SPL's (k230_boot.c) */

struct ota_records {
    uint32_t valid_a, version_a, valid_b, version_b;
};

struct ota_begin {
    char slot;                        /* 'A' or 'B': the one written now (not the one that boots) */
    char boots;                       /* the slot the SPL boots now */
    uint32_t version;                 /* the version to commit: the booting slot's + 1 */
};

#endif
