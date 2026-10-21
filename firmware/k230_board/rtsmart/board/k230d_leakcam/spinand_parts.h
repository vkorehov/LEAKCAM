/* LEAKCAM W25N02KV (256 MiB, 2048 + 128 B pages, 64 pages per block) MTD partitions for UFFS.
 * Same layout as boards/k230d_leakcam/genimage-spinand.cfg and the U-Boot device tree:
 * 0 SPL a, 512K SPL b, 896K TOC (direct boot), 1M ota_meta, 2M U-Boot, 4M env,
 * 5M RT-Smart slot A, 25M slot B, then the two UFFS filesystems. 104M .. 256M stays unused: room for
 * bad-block replacement, never part of a filesystem. UFFS scans every block at mount (~0.38 ms per block),
 * so /sdcard is only as big as programs + two history images need (tests/test021, 2026-10-21). */
{ "nand0", 48 * 1024 * 1024,  20 * 1024 * 1024 }, // bin,    48M ~ 68M
{ "nand1", 72 * 1024 * 1024,  32 * 1024 * 1024 }, // sdcard, 72M ~ 104M (programs, image history)
