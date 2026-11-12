/*
 * The cameras' raw frames 10-bit packed (firmware/VIDEO.txt; user 2026-11-12: "lets try patch first"): VICAP's
 * offline mode writes each raw frame to memory and the ISP reads it back; with 2 raw buffers a camera drops every
 * other frame (7.5 fps), 3 give the native 15 fps, and with the library's 16-bit words (9.8 MB a frame) two cameras'
 * three do not fit the 75 MB media memory. Packed (3248 B a row, 6.3 MB a frame) they do: tests/test147, both
 * cameras 15 fps, 67.5 MB.
 *
 * The closed ISP library (libisp.a, VeriSilicon CamEngine, linked into this program) sets the raw format and line
 * lengths itself; its register writes are changed on their way, nothing else (register map: VeriSilicon ISP, the
 * same as nxp-imx/isp-vvcam vvcam/isp/mrv_all_regs.h; skill k230-vicap-isp-patch):
 *   - link-time wrappers (rtsmart/Makefile -Wl,--wrap=): VirtualHalWriteReg (isp_write_reg's: the read DMA),
 *     SwitchUpdateCommonRegister (the registers both cameras share, merged into a shadow with an OR),
 *     VsiCamDeviceGetBufferSize (the raw buffer size kd_mpi_isp_init checks VICAP's buffer_size against);
 *   - VIHalWriteReg replaced: local to isp_mcm.o, made weak in a copy of libisp.a (rtsmart/Makefile); it stores
 *     every write directly with the caller's value after the merge;
 *   - rawpack_writers(): the writers' line lengths and sizes, set once by the library through a path no wrapper
 *     reaches, written after the cameras' start through the library's own mapping of the ISP.
 */
#include <stdint.h>
#include <stdio.h>

#include "cap.h"

#define W        2592
#define H        1944
#define LL16     (W * 2)                         /* the library's rows: 16-bit words */
#define LL10     ((W * 10 / 8 + 15) & ~15)      /* packed 10-bit, rows padded to 16 B: 3248 (3240 sheared the
                                                 * picture by 8 B a row) */

/* a write to the MCM blocks (0x1200.., 0x1600..) changed to the packed layout; others pass */
static uint32_t patch(uint32_t addr, uint32_t v)
{
    switch (addr) {
    case 0x1604:                                /* miv2_mcm_fmt: rd / wr0 / wr1 raw bit (bits 0, 4, 8), 4 = 16-bit */
        for (int s = 0; s <= 8; s += 4)
            if (((v >> s) & 0xf) == 4 || ((v >> s) & 0xf) == 5)   /* 5: 4 | 1 from the shadow's OR */
                v = (v & ~(0xfu << s)) | (1u << s);                 /* 1: 10-bit */
        return v & ~0x3f000u;                   /* every path unaligned: packed */
    case 0x1620: case 0x164c: case 0x1674: case 0x1690:   /* line lengths: wr0, wr1, read DMA, its lval */
        return v == LL16 ? LL10 : v;
    case 0x162c: case 0x1658: case 0x1678:                /* picture sizes */
        return v == (uint32_t)LL16 * H ? (uint32_t)LL10 * H : v;
    }
    return v;
}

static int mcm(uint32_t addr)
{
    return (addr >= 0x1600 && addr < 0x1700) || (addr >= 0x1200 && addr < 0x1300);
}

int __real_VirtualHalWriteReg(void *hal, uint32_t addr, uint32_t value);
int __real_SwitchUpdateCommonRegister(void *h, uint32_t addr, uint32_t value);
int __real_VsiCamDeviceGetBufferSize(void *dev, int type, uint32_t *size);

int __wrap_VirtualHalWriteReg(void *hal, uint32_t addr, uint32_t value)
{
    return __real_VirtualHalWriteReg(hal, addr, mcm(addr) ? patch(addr, value) : value);
}

/* the shadow of 0x1604 in the switch handle (isp_switch_api.o: stored |= value & 0x3cff0, at 0x9000 - 1476): the
 * library's first 16-bit write stays in it through the OR, so it is patched too before every merge */
#define SHADOW_1604 (0x9000 - 1476)

int __wrap_SwitchUpdateCommonRegister(void *h, uint32_t addr, uint32_t value)
{
    if (addr == 0x1604 && h) {
        uint32_t *sh = (uint32_t *)((char *)h + SHADOW_1604);
        *sh = patch(0x1604, *sh);
    }
    return __real_SwitchUpdateCommonRegister(h, addr, mcm(addr) ? patch(addr, value) : value);
}

/* type 5: the raw frames */
int __wrap_VsiCamDeviceGetBufferSize(void *dev, int type, uint32_t *size)
{
    int rc = __real_VsiCamDeviceGetBufferSize(dev, type, size);
    if (rc == 0 && type == 5 && size && *size >= (uint32_t)LL16 * H)
        *size = LL10 * H;
    return rc;
}

/* VIHalWriteReg, replaced (from its disassembly, 2026-11-12): the cameras' common registers through
 * SwitchUpdateCommonRegister, then every write stored directly through the mapping of the context's ISP
 * (ispFdAry / fpga_regs_mmap); returns 0 as the original's usual path (its debug print, the context's FILE at +104,
 * is left out) */
extern int ispFdAry[4];
extern void *fpga_regs_mmap[4];
void *SwitchGetHandle(uint8_t id);

static int common(uint32_t a)
{
    if (a > 0x160c)
        return ((a - 0x5004) & ~4u) == 0 || a == 0x16c0;
    if (a > 0x15ff)
        return (0x1111u >> (a - 0x1600)) & 1;   /* 0x1600, 0x1604, 0x1608, 0x160c */
    return (a & ~0x100u) == 0x1200;
}

int VIHalWriteReg(void *ctx, uint32_t addr, uint32_t value)
{
    if (mcm(addr))
        value = patch(addr, value);
    void *h = SwitchGetHandle(*((uint8_t *)ctx + 4));
    if (h && common(addr))
        __wrap_SwitchUpdateCommonRegister(h, addr, value);
    int id = *(int *)((char *)ctx + 8);
    for (int i = 0; i < 4; i++)
        if (ispFdAry[i] == id) {
            *(volatile uint32_t *)((char *)fpga_regs_mmap[i] + (addr & ~3u)) = value;
            break;
        }
    return 0;
}

void rawpack_writers(void)
{
    volatile uint32_t *r = fpga_regs_mmap[0];
    if (!r) {
        fprintf(stderr, "rawpack: no ISP mapping\n");
        return;
    }
#define R(off) r[(off) / 4]
    R(0x1604) = patch(0x1604, R(0x1604));       /* the merge may have left the 16-bit bit set */
    R(0x1620) = LL10;
    R(0x162c) = LL10 * H;
    R(0x164c) = LL10;
    R(0x1658) = LL10 * H;
    R(0x1600) |= 0x44;                          /* MCM_WR_CFG_UPD, MCM_RD_CFG_UPD: the new configuration taken */
#undef R
}
