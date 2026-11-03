/*
 * The setup firmware's sleep: see setup_sleep.h. From bouffalo_sdk examples/pmu/wl_ble_lp (main.c), changed for rev 1
 * (tests/test120, test121; skill bl616-ble-low-power).
 */
#include "setup_sleep.h"

#include "FreeRTOS.h"
#include "task.h"

#include "bflb_clock.h"
#include "bflb_gpio.h"
#include "board.h"
#include "bl_lp.h"
#include "bl616_glb.h"
#include "pm_manager.h"

#include "bluetooth.h"
#include "conn.h"
#include "conn_internal.h"   /* conn->type */

#include "ble_pairing.h"
#include "setup_boot.h"   /* SETUP_ADV_SLEEP_MS */
#include "bflb_wdg.h"
#include "board_pins.h"
#include "usb_console2.h"
#include "wdg_guard.h"

static volatile struct setup_sleep_stats st;
static bool usb_off, sleeping;
static TaskHandle_t usb_plug_handle;

/* -Wl,--wrap=vApplicationSleep (CMakeLists.txt): the SDK's idle hook (tickless_accurate.c) behind a BLE check and the
 * counters. BLE up but idle: the SDK's sleep path asks the controller for its next deadline (btble_controller_sleep(0))
 * before any veto, and with no deadline that put the BLE core to sleep for good: after any software reset its init
 * spun forever in rwip_time_get, only the EN pin cleared it (tests/test120, 2026-11-01, case D). Idle as with tickless
 * off instead. */
void __real_vApplicationSleep(TickType_t idle);
void __wrap_vApplicationSleep(TickType_t idle)
{
    if (!ble_pairing_sleep_ok()) {
        st.vetoed++;
        __WFI();
        return;
    }
    st.calls++;
    if (idle <= configEXPECTED_IDLE_TIME_BEFORE_SLEEP)
        st.short_idle++;
    if (idle > st.max_idle)
        st.max_idle = idle;
    __real_vApplicationSleep(idle);
}

/* the SDK's power manager asks whether the app holds buffers that keep it awake */
int bflb_pm_app_check(void)
{
    return pm_pbufc_check();
}

static void awake(void)
{
    pm_disable_tickless();
    wdg_guard_feed_hold(false);
    if (usb_off) {
        usb_off = false;
        usb_console_resume();
    }
}

bool setup_usb_present(void)
{
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    bflb_gpio_init(gpio, PIN_USB_PGOOD, GPIO_INPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_0);
    return !bflb_gpio_read(gpio, PIN_USB_PGOOD);
}

/* The watchdog's feeding holds for the sleep: its 1 s task woke the chip twice per 2 s advertising interval (180 wakes
 * in 120 s, test120). Each wake restarts its counter (lp_exit), so an app hung while awake is still reset. */
void setup_sleep_begin(bool usb_too)
{
    if (!usb_too && setup_usb_present()) {
        printf("setup: USB power present: no sleep, the full firmware's case\r\n");
        return;
    }
    sleeping = true;
    wdg_guard_feed_hold(true);
    pm_enable_tickless();
}

void setup_sleep_pause(bool pause)
{
    if (pause) {
        awake();
    } else if (sleeping) {
        wdg_guard_feed_hold(true);
        pm_enable_tickless();
    }
}

void setup_sleep_end(void)
{
    sleeping = false;
    awake();
}

void setup_sleep_stats(struct setup_sleep_stats *s)
{
    *s = st;
}

/* USB-C 1 plugged in during the sleep (test121): PGOOD (IO3) goes low and wakes the chip from PDS. The wake callback
 * runs in the low-power layer's software interrupt (bl616_lp_callback.c): it only notifies usb_plug_task */
static void usb_wake_cb(uint64_t io_bits)
{
    BaseType_t woken = pdFALSE;

    if (!(io_bits & ((uint64_t)1 << PIN_USB_PGOOD)))
        return;
    st.usb_wakes++;
    vTaskNotifyGiveFromISR(usb_plug_handle, &woken);
    portYIELD_FROM_ISR(woken);
}

/* USB-C 1 pulled while awake (plugged in at the start or since): back to the battery's 2 s advertising and the sleep.
 * Only the plug wakes the chip (PGOOD's falling edge); the pull is read here every second while awake: before, the
 * chip stayed awake at 100 ms advertising on the cell until a reset (tests/test116, 2026-11-03). Not under a phone's
 * connection: its setup ends in the switch to the full firmware anyway. */
static void usb_plug_task(void *arg)
{
    (void)arg;
    bool usb = setup_usb_present();
    while (1) {
        /* asleep: only the plug's wake (a 1 s timeout would wake the chip every second) */
        if (ulTaskNotifyTake(pdTRUE, sleeping ? portMAX_DELAY : pdMS_TO_TICKS(1000))) {
            setup_sleep_end();
            setup_usb_plugged();
            usb = true;
            continue;
        }
        if (sleeping)
            continue;
        bool now = setup_usb_present();
        if (usb && !now) {
            if (ble_pairing_connected())
                continue;                 /* under a phone: read again once it has left */
            printf("setup: USB power gone: advertising every %d ms, sleep\r\n", SETUP_ADV_SLEEP_MS);
            ble_pairing_adv_interval(SETUP_ADV_SLEEP_MS);
            setup_sleep_begin(false);
        }
        usb = now;
    }
}

/* only IO3 wakes, on its falling edge; no pulls added in sleep: PGOOD has its own 100 k to 3V3_SLEEP (board_pins.h),
 * and the other pins keep what the firmware set (the SDK's io_test example pulls every group: IO0 is the K230 reset) */
static void usb_wake_init(void)
{
    static bl_lp_io_cfg_t cfg = {
        .io_0_15_ie = BL_LP_IO_INPUT_ENABLE,
        .io_16_ie = BL_LP_IO_INPUT_DISABLE,
        .io_17_ie = BL_LP_IO_INPUT_DISABLE,
        .io_18_ie = BL_LP_IO_INPUT_DISABLE,
        .io_19_ie = BL_LP_IO_INPUT_DISABLE,
        .io_20_34_ie = BL_LP_IO_INPUT_DISABLE,
        .io_0_7_pds_trig_mode = BL_LP_PDS_IO_TRIG_SYNC_FALLING_EDGE,
        .io_8_15_pds_trig_mode = BL_LP_PDS_IO_TRIG_NONE,
        .io_16_19_aon_trig_mode = BL_LP_AON_IO_TRIG_NONE,
        .io_20_27_pds_trig_mode = BL_LP_PDS_IO_TRIG_NONE,
        .io_28_34_pds_trig_mode = BL_LP_PDS_IO_TRIG_NONE,
        .io_0_15_res = BL_LP_IO_RES_NONE,
        .io_16_res = BL_LP_IO_RES_NONE,
        .io_17_res = BL_LP_IO_RES_NONE,
        .io_18_res = BL_LP_IO_RES_NONE,
        .io_19_res = BL_LP_IO_RES_NONE,
        .io_20_34_res = BL_LP_IO_RES_NONE,
        .io_wakeup_unmask = (uint64_t)1 << PIN_USB_PGOOD,
    };

    if (xTaskCreate(usb_plug_task, "usb_plug", 512, NULL, 20, &usb_plug_handle) != pdPASS ||
        bl_lp_io_wakeup_cfg(&cfg) != 0)
        vAssertCalled();
    bl_lp_wakeup_io_int_register(usb_wake_cb);
}

/* The USB console is detached once for the whole sleep, not around every advertising wake (every 2 s): the host saw
 * a detach and attach per advertisement and VMware takes ~4 s to attach (test120) */
static int lp_enter(void *arg)
{
    (void)arg;
    st.entered++;
    uint16_t c = bflb_wdg_get_countervalue(bflb_device_get_by_name("watchdog0"));
    if (c > st.wdg_enter_max)
        st.wdg_enter_max = c;
    if (!usb_off) {
        usb_console_suspend();
        usb_off = true;
    }
    wdg_guard_pause(true);
    return 0;
}

/* The clocks after a wake, as the example's set_cpu_bclk_80M_and_gate_clk(), plus the USB and watchdog clocks: with
 * CONFIG_LPAPP the board init gates everything else (peripheral_clock_init_lp(): USB only with CONFIG_BSP_USB, the
 * watchdog never: wdg_guard_pause() ungates its own after board_recovery(); the first builds never reached USB,
 * test120) */
static void clocks_after_wake(void)
{
    uint32_t v;

    GLB_Set_MCU_System_CLK_Div(0, 3);
    CPU_Set_MTimer_CLK(ENABLE, BL_MTIMER_SOURCE_CLOCK_MCU_CLK,
                       Clock_System_Clock_Get(BL_SYSTEM_CLOCK_MCU_CLK) / 1000000 - 1);

    v = 0;
    v = BL_SET_REG_BITS_VAL(v, GLB_CGEN_M_CPU, 1);
    v = BL_SET_REG_BITS_VAL(v, GLB_CGEN_M_DMA, 1);
    v = BL_SET_REG_BITS_VAL(v, GLB_CGEN_M_SEC, 1);
    v = BL_SET_REG_BITS_VAL(v, GLB_CGEN_M_SDU, 1);
    BL_WR_REG(GLB_BASE, GLB_CGEN_CFG0, v);

    v = 0;
    v = BL_SET_REG_BITS_VAL(v, GLB_CGEN_S1_EF_CTRL, 1);
    v = BL_SET_REG_BITS_VAL(v, GLB_CGEN_S1_SF_CTRL, 1);
    v = BL_SET_REG_BITS_VAL(v, GLB_CGEN_S1_DMA, 1);
    v = BL_SET_REG_BITS_VAL(v, GLB_CGEN_S1A_UART0, 1);
    v = BL_SET_REG_BITS_VAL(v, GLB_CGEN_S1A_UART1, 1);
    v = BL_SET_REG_BITS_VAL(v, GLB_CGEN_S1_SEC_ENG, 1);
    BL_WR_REG(GLB_BASE, GLB_CGEN_CFG1, v);

    v = 0;
    v = BL_SET_REG_BITS_VAL(v, GLB_CGEN_S2_WIFI, 1);
    v = BL_SET_REG_BITS_VAL(v, GLB_CGEN_S1_EXT_EMI_MISC, 1);
    v = BL_SET_REG_BITS_VAL(v, GLB_CGEN_S1_EXT_PIO, 1);
    BL_WR_REG(GLB_BASE, GLB_CGEN_CFG2, v);

    PERIPHERAL_CLOCK_USB_ENABLE();
    GLB_Set_USB_CLK_From_WIFIPLL(1);
}

extern void vPortSetupTimerInterrupt(void);

/* Not on LPFW_WAKEUP_BLE as in the example: on the BL616 every advertising event wakes the app, so the example's
 * pm_disable_tickless() there ended the sleep at the first advertisement (test120); a connection ends it. No JTAG
 * remux of IO0-IO3 either: IO0 holds the K230 in reset, IO3 is PGOOD. The console is on USB (usb_console2.c). */
static int lp_exit(void *arg)
{
    extern TaskHandle_t rxl_process_task_hd;
    BaseType_t woken = pdFALSE;

    (void)arg;
    st.exited++;
    clocks_after_wake();
    board_recovery();
    vPortSetupTimerInterrupt();
    wdg_guard_pause(false);
    uint16_t c = bflb_wdg_get_countervalue(bflb_device_get_by_name("watchdog0"));
    if (c > st.wdg_exit_max)
        st.wdg_exit_max = c;

    if (bl_lp_get_wake_reason() & LPFW_WAKEUP_WIFI_BROADCAST) {
        vTaskNotifyGiveFromISR(rxl_process_task_hd, &woken);
        portYIELD_FROM_ISR(woken);
    } else {
        pm_alloc_mem_reset();
    }
    return 0;
}

void setup_sleep_init(void)
{
    bl_lp_sys_callback_register(lp_enter, NULL, lp_exit, NULL);
    usb_wake_init();
}

static void on_connected(struct bt_conn *conn, u8_t err)
{
    if (err || conn->type != BT_CONN_TYPE_LE)
        return;
    setup_sleep_pause(true);
}

static void on_disconnected(struct bt_conn *conn, u8_t reason)
{
    (void)reason;
    if (conn->type != BT_CONN_TYPE_LE)
        return;
    setup_sleep_pause(false);
}

static struct bt_conn_cb conn_cb = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

/* beside ble_pairing.c's own callbacks */
void setup_sleep_ble_register(void)
{
    bt_conn_cb_register(&conn_cb);
}
