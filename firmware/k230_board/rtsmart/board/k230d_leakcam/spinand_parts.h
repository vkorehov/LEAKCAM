/* LEAKCAM W25N02KV (256 MiB, 2048 + 128 B pages, 64 pages per block) MTD partitions for UFFS.
 * Same layout as boards/k230d_leakcam/genimage-spinand.cfg and the U-Boot device tree:
 * 0 SPL a, 512K SPL b, 896K TOC (direct boot), 1M ota_meta, 2M U-Boot, 4M env,
 * 5M RT-Smart slot A, 25M slot B, then the two UFFS filesystems. 104M .. 256M stays unused: room for
 * bad-block replacement, never part of a filesystem. UFFS scans every block at mount (~0.38 ms per block),
 * so /sdcard is only as big as programs + two history images need (tests/test022, 2026-10-21). The slots and ota_meta
 * are raw MTD devices for /dev/ota (drivers/ota_nand), never mounted. */
{ "ota_meta", 1 * 1024 * 1024,   256 * 1024 },     // A/B slot records (raw, /dev/ota: DESIGN.md 5.3.7)
{ "rtt_a",    5 * 1024 * 1024,  20 * 1024 * 1024 }, // RT-Smart slot A (raw, /dev/ota writes the inactive slot)
{ "rtt_b",   25 * 1024 * 1024,  20 * 1024 * 1024 }, // RT-Smart slot B
{ "nand0", 48 * 1024 * 1024,   8 * 1024 * 1024 }, // bin,    48M ~ 56M (3.3 MB of ISP files; 56M ~ 72M unused)
{ "nand1", 72 * 1024 * 1024,  32 * 1024 * 1024 }, // sdcard, 72M ~ 104M (programs, image history)
