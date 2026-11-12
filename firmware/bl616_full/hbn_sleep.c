#include "hbn_sleep.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "bflb_acomp.h"
#include "bflb_mtimer.h"
#include "bl616_hbn.h"
#include "bl616_pm.h"
#include "shell.h"

#include "climate.h"
#include "k230_power.h"
#include "leak_check.h"
#include "metrics.h"
#include "presence.h"
#include "rtc_clock.h"
#include "usb_power.h"

#define MAGIC        0x534c5031u     /* "SLP1" */
#define F_SLEEPING   1u              /* set right before HBN: the next boot is its wake */
#define F_WET        2u              /* the probe was wet (its edges: leak_check.c) */
#define F_OFF        4u              /* `hbn off` (the bench: the console stays on battery), until the next reset */
#define SLEEP_MIN_S  10              /* shorter is not worth a wake (a boot, the radio's calibration) */
#define POLL_MS      250

/* full firmware only: sorted last ("zz"), after the records both firmwares share at the same addresses */
static struct {
    uint32_t magic, flags;
    uint32_t checkin_s, leak_s;      /* due, RTC s */
    uint32_t missed;                 /* failed check-ins in a row (presence.c) */
} st __attribute__((section(".hbn_noinit.zzsleep")));
static unsigned woke;
static uint32_t slept_s;             /* the last sleep asked, for `sleep` */

unsigned hbn_sleep_boot(void)
{
    bool asleep = st.magic == MAGIC && (st.flags & F_SLEEPING);
    unsigned w = 0;
    if (HBN_Get_INT_State(HBN_INT_ACOMP1) == SET)
        w |= HBN_WAKE_LEAK;
    if (HBN_Get_INT_State(HBN_INT_ACOMP0) == SET)
        w |= HBN_WAKE_USB;
    if (HBN_Get_INT_State(HBN_INT_RTC) == SET)
        w |= HBN_WAKE_RTC;
    HBN_Clear_IRQ(HBN_INT_ACOMP1);
    HBN_Clear_IRQ(HBN_INT_ACOMP0);
    HBN_Clear_IRQ(HBN_INT_RTC);
    if (asleep) {
        woke = w ? w : HBN_WAKE_RTC;
        st.flags &= ~F_SLEEPING;
        metrics_hbn_wake();
    } else {                             /* a reset or a power-on: due now, the bench switch off again */
        memset(&st, 0, sizeof(st));
        st.magic = MAGIC;
        woke = 0;
    }
    return woke;
}

unsigned hbn_sleep_woke(void)
{
    return woke;
}

uint32_t hbn_sleep_checkin_due(void) { return st.checkin_s; }
void hbn_sleep_checkin_set(uint32_t s) { st.checkin_s = s; }
uint32_t hbn_sleep_leak_due(void) { return st.leak_s; }
void hbn_sleep_leak_set(uint32_t s) { st.leak_s = s; }
bool hbn_sleep_wet(void) { return st.flags & F_WET; }
void hbn_sleep_wet_set(bool wet) { st.flags = wet ? st.flags | F_WET : st.flags & ~F_WET; }
unsigned hbn_sleep_missed(void) { return st.missed; }
void hbn_sleep_missed_set(unsigned n) { st.missed = n; }

static uint32_t left(uint32_t due, uint32_t now)
{
    int32_t l = (int32_t)(due - now);
    return l > 0 ? (uint32_t)l : 0;
}

/* s until the first of the check-in, the leak check and the humidity reading */
static uint32_t earliest(uint32_t now)
{
    uint32_t a = left(st.checkin_s, now), b = left(st.leak_s, now), c = left(climate_due_s(), now);
    return a < b ? (a < c ? a : c) : (b < c ? b : c);
}

/* why the board stays awake now, NULL = it may sleep */
static const char *awake_why(void)
{
    if (st.flags & F_OFF)
        return "hbn off";
    if (usb_present())
        return "USB power";
    if (k230_power_is_on())
        return "the K230 on";
    if (leak_check_busy())
        return "a leak check";
    if (presence_busy())
        return "a check-in";
    return NULL;
}

/* HBN until the RTC (s), a probe edge or the USB plug; the wake is a reset. The arming as tests/test031-035 proved it */
static void enter(uint32_t s)
{
    bool wet = leak_check_wet();
    (void)rtc_clock_xtal();
    printf("sleep: HBN %lu s (probe %s)\r\n", (unsigned long)s, wet ? "wet" : "dry");
    bflb_mtimer_delay_ms(20);            /* the console sends the line */
    vTaskSuspendAll();
    __disable_irq();
    /* the probe's next edge: wet (falling) while dry, dry again (rising) while wet (test034); USB: the plug (PGOOD
     * falling, test035) */
    HBN_Clear_IRQ(HBN_INT_ACOMP1);
    HBN_Disable_AComp_IRQ(AON_ACOMP1_ID, wet ? HBN_ACOMP_INT_EDGE_NEGEDGE : HBN_ACOMP_INT_EDGE_POSEDGE);
    HBN_Enable_AComp_IRQ(AON_ACOMP1_ID, wet ? HBN_ACOMP_INT_EDGE_POSEDGE : HBN_ACOMP_INT_EDGE_NEGEDGE);
    HBN_Clear_IRQ(HBN_INT_ACOMP0);
    HBN_Disable_AComp_IRQ(AON_ACOMP0_ID, HBN_ACOMP_INT_EDGE_POSEDGE);
    HBN_Enable_AComp_IRQ(AON_ACOMP0_ID, HBN_ACOMP_INT_EDGE_NEGEDGE);
    if (usb_present() || leak_check_wet() != wet) {   /* changed while arming: that edge is gone, stay awake */
        __enable_irq();
        (void)xTaskResumeAll();
        printf("sleep: USB or the probe changed while arming: awake\r\n");
        return;
    }
    /* HBN gives GPIO16-19 hardware pulls unless disabled: on IO16/17 (Y3) they stop the crystal, the RTC never wakes
     * the chip (2026-10-22); pm_hbn_mode_enter() does not disable them */
    HBN_Hw_Pu_Pd_Cfg(DISABLE);
    st.flags |= F_SLEEPING;
    HBN_Set_HRAM_Ret();                  /* last: no HBN RAM access after it (the HBN RAM rule, tests/test021) */
    pm_hbn_mode_enter(PM_HBN_LEVEL_0, (uint64_t)s * RTC_HZ);
    for (;;) {
    }
}

static void sleep_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        if (awake_why())
            continue;
        uint32_t s = earliest(rtc_clock_s());
        if (s < SLEEP_MIN_S)
            continue;
        slept_s = s;
        enter(s);
    }
}

void hbn_sleep_start(void)
{
    if (xTaskCreate(sleep_task, "sleep", 768, NULL, 1, NULL) != pdPASS)
        vAssertCalled();
}

/* `hbn`: the state; `hbn off`: awake until the next reset (the bench's console on battery); `hbn on` (the shell's own
 * `sleep` is the SDK's delay) */
static void cmd_hbn(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "off"))
        st.flags |= F_OFF;
    else if (argc > 1 && !strcmp(argv[1], "on"))
        st.flags &= ~F_OFF;
    uint32_t now = rtc_clock_s();
    const char *why = awake_why();
    printf("hbn: %s, %s; next: check-in in %ld s, leak check in %ld s, reading in %ld s; this boot %s, last asked "
           "%lu s\r\n", st.flags & F_OFF ? "off" : "on", why ? why : "may sleep", (long)left(st.checkin_s, now),
           (long)left(st.leak_s, now), (long)left(climate_due_s(), now),
           woke ? "a wake from HBN" : "a reset", (unsigned long)slept_s);
}
SHELL_CMD_EXPORT_ALIAS(cmd_hbn, hbn, HBN sleep on battery: state [off | on]);
