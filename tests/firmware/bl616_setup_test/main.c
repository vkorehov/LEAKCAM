/*
 * The setup firmware's test bed (tests/test120, test121): firmware/bl616_setup's sources with test commands instead of
 * its main.c. The start runs on a command (`lp_boot`), so a hang in a step still leaves `make flash` working.
 *
 *   lp_boot                 the setup firmware's start (setup_boot()): low power, RF, Wi-Fi, the product BLE
 *   wakeup_timer <ms> [usb] a sleep window of <ms>; `usb`: also with USB power (current through the USB-C 1 meter)
 *   lp_pwr                  the last window's time in PDS / awake and the SDK's predicted current
 *   lp_sleepstat            the sleep counters (setup_sleep_stats()), USB power
 *   lp_task <name>          a task's saved context (PC and return addresses for addr2line)
 *   lp_blecore, lp_pds, lp_reset   the BLE core wedge experiments (test120 HISTORY; out once the rule has held)
 *   pt_ble ...              the power test firmware's BLE commands (pt_ble.c: pin, creds, adv)
 */
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "bflb_mtimer.h"
#include "board.h"
#include "bl_lp.h"
#include "bl616_glb.h"
#include "bl616_hbn.h"
#include "bl616_pds.h"
#include "pm_manager.h"
#include "shell.h"

#include "crash_rec.h"
#include "setup_boot.h"
#include "setup_sleep.h"
#include "wdg_guard.h"

static volatile bool booted;

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *size)
{
    static StaticTask_t idle_tcb;
    static StackType_t idle_stack[1024];

    *tcb = &idle_tcb;
    *stack = idle_stack;
    *size = 1024;
}

/* pt_ble.c's helpers (tests.h): RF and easyflash come from `lp_boot` */
bool pt_radio_init(void)
{
    if (!booted)
        printf("pt_ble: run lp_boot first\r\n");
    return booted;
}

int pt_ef_once(void)
{
    return booted ? 0 : -1;
}

bool usb_present(void)
{
    return setup_usb_present();
}

void ble_credentials_stored(void)
{
    printf("lp: credentials stored\r\n");
}

/* printed before the USB console has attached again: lost on the bench, `lp_sleepstat` counts it (test121) */
void setup_usb_plugged(void)
{
    printf("lp: USB plugged\r\n");
}

static void boot_task(void *arg)
{
    (void)arg;
    setup_boot();
    booted = true;
    printf("lp: boot done\r\n");
    vTaskDelete(NULL);
}

static void cmd_lp_boot(int argc, char **argv)
{
    static bool started;

    (void)argc;
    (void)argv;
    if (started) {
        printf("lp_boot: already run\r\n");
        return;
    }
    started = true;
    if (xTaskCreate(boot_task, "lp_boot", 1024, NULL, 10, NULL) != pdPASS)
        vAssertCalled();
}
SHELL_CMD_EXPORT_ALIAS(cmd_lp_boot, lp_boot, the setup firmware start (test120));

/* the window's figures, taken at its end and kept: printed then they went out before the USB console was back */
static bl_lp_info_t window_info;

static void window_end(TimerHandle_t t)
{
    bl_lp_info_get(&window_info);
    setup_sleep_end();
    (void)xTimerDelete(t, 0);
}

static void cmd_wakeup_timer(int argc, char **argv)
{
    TimerHandle_t t;

    if (argc < 2 || atoi(argv[1]) <= 0) {
        printf("wakeup_timer <ms> [usb]\r\n");
        return;
    }
    t = xTimerCreate("window", pdMS_TO_TICKS(atoi(argv[1])), pdFALSE, NULL, window_end);
    if (t == NULL || xTimerStart(t, 0) != pdPASS)
        vAssertCalled();
    bl_lp_info_clear();
    setup_sleep_begin(argc > 2 && !strcmp(argv[2], "usb"));
}
SHELL_CMD_EXPORT_ALIAS(cmd_wakeup_timer, wakeup_timer, a sleep window <ms> [usb]);

/* the SDK example's estimate from the time split: its fixed figures (PDS 80 uA, lpfw 38 mA, app 57 mA), not our board's */
static void cmd_lp_pwr(int argc, char **argv)
{
    bl_lp_info_t *i = &window_info;

    (void)argc;
    (void)argv;
    if (i->time_total_us == 0) {
        printf("lp_pwr: no window yet\r\n");
        return;
    }
    printf("Total time %llums\r\nPDS sleep: %llums\r\nLPFW active: %llums\r\nAPP active: %llums\r\n",
           (unsigned long long)(i->time_total_us / 1000), (unsigned long long)(i->sleep_pds_us / 1000),
           (unsigned long long)(i->active_lpfw_us / 1000), (unsigned long long)(i->active_app_us / 1000));
    printf("Predict current: %lluuA\r\n",
           (unsigned long long)((i->sleep_pds_us * 80 + i->active_lpfw_us * 38000 + i->active_app_us * 57000) /
                                i->time_total_us));
}
SHELL_CMD_EXPORT_ALIAS(cmd_lp_pwr, lp_pwr, test120: time split and predicted current of the last sleep window);

static void cmd_lp_sleepstat(int argc, char **argv)
{
    struct setup_sleep_stats s;

    (void)argc;
    (void)argv;
    setup_sleep_stats(&s);
    printf("USB plug wakes %lu, USB power %s\r\n", (unsigned long)s.usb_wakes, setup_usb_present() ? "yes" : "no");
    printf("sleep hook calls %lu, idle <= %lu ms %lu, max idle %lu ms, sleeps entered %lu, BLE idle vetoes %lu, "
           "pm event %d\r\n",
           (unsigned long)s.calls, (unsigned long)configEXPECTED_IDLE_TIME_BEFORE_SLEEP, (unsigned long)s.short_idle,
           (unsigned long)s.max_idle, (unsigned long)s.entered, (unsigned long)s.vetoed, (int)bl_pm_event_get());
}
SHELL_CMD_EXPORT_ALIAS(cmd_lp_sleepstat, lp_sleepstat, test120: tickless sleep attempts and entries);

/* a task's saved context while it is not running: the words from its saved stack pointer (the first member of the
 * FreeRTOS TCB) up; the PC and return addresses among them resolve with addr2line (test120: the BLE start stuck in
 * btble_controller_init, invisible to the crash recorder because everything else ran) */
static void cmd_lp_task(int argc, char **argv)
{
    TaskHandle_t t = argc > 1 ? xTaskGetHandle(argv[1]) : NULL;

    if (t == NULL) {
        printf("lp_task: no such task\r\n");
        return;
    }
    uint32_t *sp = *(uint32_t **)t;
    printf("lp_task %s sp %08lx:", argv[1], (unsigned long)(uintptr_t)sp);
    for (int i = 0; i < 40; i++)
        printf("%s%08lx", i % 8 ? " " : "\r\n", (unsigned long)sp[i]);
    printf("\r\n");
}
SHELL_CMD_EXPORT_ALIAS(cmd_lp_task, lp_task, test120: saved context of a task <name>);

/* the BLE core's deep-sleep control (DEEPSLCNTL, 0x28000030) and its own resets: none cleared the wedged core (test120) */
#define BLE_DEEPSLCNTL ((volatile uint32_t *)0x28000030)
static void cmd_lp_blecore(int argc, char **argv)
{
    printf("lp_blecore: DEEPSLCNTL %08lx\r\n", (unsigned long)*BLE_DEEPSLCNTL);
    if (argc > 1 && !strcmp(argv[1], "wake")) {
        *BLE_DEEPSLCNTL |= 1u << 15;
        for (int i = 0; i < 100 && (*BLE_DEEPSLCNTL & (1u << 15)); i++)
            bflb_mtimer_delay_ms(1);
        printf("lp_blecore: DEEPSLCNTL %08lx after the wake request\r\n", (unsigned long)*BLE_DEEPSLCNTL);
    } else if (argc > 1 && (!strcmp(argv[1], "btdm") || !strcmp(argv[1], "ble2"))) {
        GLB_AHB_MCU_Software_Reset(argv[1][1] == 't' ? GLB_AHB_MCU_SW_BTDM : GLB_AHB_MCU_SW_BLE2);
        printf("lp_blecore: %s reset, DEEPSLCNTL %08lx, 0x28000100 %08lx\r\n", argv[1],
               (unsigned long)*BLE_DEEPSLCNTL, (unsigned long)*(volatile uint32_t *)0x28000100);
    }
}
SHELL_CMD_EXPORT_ALIAS(cmd_lp_blecore, lp_blecore, test120: BLE core deep-sleep control [wake | btdm | ble2]);

/* the PDS block's power control (persists across a software reset); `restore` writes a freshly reset chip's values
 * (2026-11-01): did not clear the wedge (test120) */
static void cmd_lp_pds(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "restore")) {
        BL_WR_REG(PDS_BASE, PDS_CTL, 0x14006b00u);
        BL_WR_REG(PDS_BASE, PDS_CTL4, 0x0ff0f00fu);
        BL_WR_REG(PDS_BASE, PDS_CTL5, 0x00000000u);
    }
    printf("lp_pds: CTL %08lx CTL2 %08lx CTL3 %08lx CTL4 %08lx CTL5 %08lx\r\n",
           (unsigned long)BL_RD_REG(PDS_BASE, PDS_CTL), (unsigned long)BL_RD_REG(PDS_BASE, PDS_CTL2),
           (unsigned long)BL_RD_REG(PDS_BASE, PDS_CTL3), (unsigned long)BL_RD_REG(PDS_BASE, PDS_CTL4),
           (unsigned long)BL_RD_REG(PDS_BASE, PDS_CTL5));
}
SHELL_CMD_EXPORT_ALIAS(cmd_lp_pds, lp_pds, test120: PDS power control registers [restore]);

/* the resets the firmware can trigger (wdg: hang with interrupts off until the watchdog): none cleared the wedge */
static void cmd_lp_reset(int argc, char **argv)
{
    const char *k = argc > 1 ? argv[1] : "";

    printf("lp_reset: %s\r\n", k);
    bflb_mtimer_delay_ms(50);
    if (!strcmp(k, "wdg")) {
        __disable_irq();
        while (1) {
        }
    } else if (!strcmp(k, "por")) {
        GLB_SW_POR_Reset();
    } else if (!strcmp(k, "sys")) {
        GLB_SW_System_Reset();
    } else if (!strcmp(k, "cpu")) {
        GLB_SW_CPU_Reset();
    }
    printf("lp_reset: wdg | por | sys | cpu\r\n");
}
SHELL_CMD_EXPORT_ALIAS(cmd_lp_reset, lp_reset, test120: reset kinds wdg por sys cpu);

int main(void)
{
    /* HBN RAM rule (tests/test021): retention/sleep mode off before the first HBN RAM access */
    BL_WR_REG(HBN_BASE, HBN_SRAM, BL_RD_REG(HBN_BASE, HBN_SRAM) & ~(HBN_RETRAM_RET_MSK | HBN_RETRAM_SLP_MSK));
    board_init();                   /* also the USB console and its shell (defconfig) */
    crash_rec_report();
    (void)wdg_guard_start();
    vTaskStartScheduler();
    while (1) {
    }
}
