#include "rtc_clock.h"

#include <stdio.h>

#include "bflb_mtimer.h"
#include "bl616_hbn.h"
#include "hardware/hbn_reg.h"

uint64_t rtc_clock_ticks(void)
{
    uint32_t lo, hi;
    HBN_Get_RTC_Timer_Val(&lo, &hi);
    return ((uint64_t)(hi & 0xff) << 32) | lo;
}

uint32_t rtc_clock_s(void)
{
    return (uint32_t)(rtc_clock_ticks() / RTC_HZ);
}

bool rtc_clock_xtal(void)
{
    if (BL_GET_REG_BITS_VAL(BL_RD_REG(HBN_BASE, HBN_GLB), HBN_F32K_SEL) == HBN_32K_XTAL)
        return true;
    HBN_Power_On_Xtal_32K();
    HBN_32K_Sel(HBN_32K_XTAL);
    uint64_t t0 = rtc_clock_ticks();
    for (int ms = 0; ms < 3000; ms += 10) {
        bflb_mtimer_delay_ms(10);
        if (rtc_clock_ticks() - t0 > 100) {      /* ~328 ticks per 10 ms once running */
            printf("rtc: Y3 running after %d ms\r\n", ms);
            return true;
        }
    }
    HBN_32K_Sel(HBN_32K_RC);
    printf("rtc: Y3 not running after 3 s, the RTC on RC32K\r\n");
    return false;
}
