#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "mm.h"

#include <lwip/tcpip.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "bl_fw_api.h"
#include "wifi_mgmr_ext.h"
#include "wifi_mgmr.h"
#include "fhost_api.h"

#include "bflb_irq.h"
#include "bflb_mtimer.h"
#include "board.h"
#include "bl_lp.h"
#if defined(BL618DG)
#include "bl618dg_pm.h"
#include "bl618dg_glb.h"
#include "bl618dg_hbn.h"
#else
#include "bl616_pm.h"
#include "bl616_glb.h"
#include "bl616_glb_gpio.h"
#include "bl616_hbn.h"
#endif
#include "bflb_uart.h"
#include "bflb_gpio.h"
#include "bflb_clock.h"
#include "bflb_rtc.h"

#include "rfparam_adapter.h"

#include "board.h"
#include "board_rf.h"
#include "shell.h"

#include "bflb_mtd.h"
#include "bflb_boot2.h"
#include "easyflash.h"
#include "clock_manager.h"
#include "pm_manager.h"

#include "usb_console2.h"
#include "board_pins.h"
#include "ble_pairing.h"
#include "wdg_guard.h"
#include "crash_rec.h"

/* boot breadcrumbs at a fixed HBN RAM word (free in this and the bl616_powertest layout): a hang before the USB
 * console leaves its last step there across the reset; the test firmware prints it (`pt_lpcrumb`, tests/test120) */
#define LP_CRUMB ((volatile uint32_t *)(0x20010000 + 0xE00))
static void lp_crumb(uint32_t step)
{
    LP_CRUMB[0] = 0x3142504cu;           /* "LPB1" */
    LP_CRUMB[1] = step;
    LP_CRUMB[2]++;
}

#define DBG_TAG "MAIN"
#include "log.h"
#include "async_event.h"

#if defined(CFG_BLE_ENABLE)
#include "bluetooth.h"
#include "conn.h"
#include "conn_internal.h"
#include "btble_lib_api.h"
#if defined(BL618DG)
#include "bl618dg_glb.h"
#else
#include "bl616_glb.h"
#endif
#include "hci_driver.h"
#include "hci_core.h"
#include "async_event.h"
#endif

//#include "bflb_mtd.h"
//#include "easyflash.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WIFI_STACK_SIZE  (1536)
#define TASK_PRIORITY_FW (16)

#define PM_MEM_POOL_SIZE    (1460 *2)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static TaskHandle_t bluetooth_start_handle;
struct bt_conn *bleapp_default_conn;

void lp_window_pause(bool pause);

#if defined(CFG_BLE_ENABLE)
static void ble_connected(struct bt_conn *conn, u8_t err)
{
    if(err || conn->type != BT_CONN_TYPE_LE)
    {
        return;
    }
    if (!bleapp_default_conn) {
        bleapp_default_conn = conn;
    }
    /* test120: a phone connection wakes the app: no sleep and the USB console back while it is held */
    lp_window_pause(true);
}

static void ble_disconnected(struct bt_conn *conn, u8_t reason)
{
    if(conn->type != BT_CONN_TYPE_LE)
    {
        return;
    }
    if (bleapp_default_conn == conn) {
        bleapp_default_conn = NULL;
    }
    printf("%s\r\n", __func__);
    lp_window_pause(false);         /* back to low power if the window still runs */
}
static void ble_conn_param_updated(struct bt_conn *conn, u16_t interval,
			     u16_t latency, u16_t timeout)
{
    if(conn == bleapp_default_conn)
    {
        printf("%s: int 0x%04x lat %d to %d \r\n", __func__, interval, latency, timeout);
    }
}
static struct bt_conn_cb ble_conn_callbacks = {
    .connected  =   ble_connected,
    .disconnected   =   ble_disconnected,
    .le_param_updated = ble_conn_param_updated,
};

#endif
/* the controller's PDS reset hook: firmware/bl616/ble_pairing.c's empty one (the SDK's drops USB) */

/* the product's BLE (firmware/bl616/ble_pairing.c): advertising as the device ID with the provisioning service, PIN
 * pairing; every 2 s as setup mode (DESIGN.md 5.3.1). Started as the example started its own (test120 step 4) */
static void bluetooth_start_task(void *pvParameters)
{
    app_clock_init();
#if defined(CFG_BLE_ENABLE)
    ble_pairing_start();
    bt_conn_cb_register(&ble_conn_callbacks);   /* beside ble_pairing.c's: a connection pauses the sleep window */
    ble_pairing_adv_interval(2000);
    ble_pairing_advertise(true);
#endif
    vTaskDelete(NULL);
}

extern void shell_init_with_task(struct bflb_device_s *shell);

static void wifi_event_handler(async_input_event_t ev, void *priv)
{
    uint32_t code = ev->code;

    switch (code) {
        case CODE_WIFI_ON_INIT_DONE:
            LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_INIT_DONE\r\n", __func__);
            wifi_mgmr_task_start();
            break;
        case CODE_WIFI_ON_MGMR_DONE:
            LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_MGMR_DONE\r\n", __func__);
            break;
        default:
            break;
    }
}

static void wifi_start_firmware_task(void *pvParameters)
{
    (void)pvParameters;
    LOG_I("Starting wifi ...\r\n");
    async_register_event_filter(EV_WIFI, wifi_event_handler, NULL);
    wifi_task_create();
    vTaskDelay(500);
    LOG_I("Starting fhost ...\r\n");
    fhost_init();
    vTaskDelete(NULL);
}

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Functions
 ****************************************************************************/

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize)
{
    /* If the buffers to be provided to the Idle task are declared inside this
    function then they must be declared static - otherwise they will be allocated on
    the stack and so not exists after this function exits. */
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[1024];

    /* Pass out a pointer to the StaticTask_t structure in which the Idle task's
    state will be stored. */
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

    /* Pass out the array that will be used as the Idle task's stack. */
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;

    /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
    Note that, as the array is necessarily of type StackType_t,
    configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *pulIdleTaskStackSize = 1024;
}

/**********************************************************
    lp enter callback func
 **********************************************************/
/* test120: why the tickless sleep never starts (61 mA, USB console never detached, 2026-11-01): the SDK's idle hook
 * wrapped (-Wl,--wrap=vApplicationSleep) to count its calls and the idle time FreeRTOS offers, lp_enter() counts the
 * sleeps that reached the low-power firmware; `lp_sleepstat` prints both */
static volatile uint32_t sl_calls, sl_short, sl_max, sl_entered, sl_vetoed;
void __real_vApplicationSleep(TickType_t idle);
void __wrap_vApplicationSleep(TickType_t idle)
{
    /* BLE up but idle: the SDK's sleep path asks the controller for its next deadline (btble_controller_sleep(0))
     * before any veto, and with no deadline that put the BLE core to sleep for good: after a software reset its init
     * spun forever, only the EN pin cleared it (test120, 2026-11-01, case D). Idle as with tickless off instead. */
    if (!ble_pairing_sleep_ok()) {
        sl_vetoed++;
        __WFI();
        return;
    }
    sl_calls++;
    if (idle <= configEXPECTED_IDLE_TIME_BEFORE_SLEEP)
        sl_short++;
    if (idle > sl_max)
        sl_max = idle;
    __real_vApplicationSleep(idle);
}

/* The sleep window (`wakeup_timer <ms> 0`): tickless sleep between advertisements. The USB console stays off for the
 * whole window, not detached and attached again around every advertising wake (every 2 s), and the watchdog feed
 * task holds. A phone connection pauses it (awake, console back) until the phone leaves; the window's end stops it. */
static bool usb_off, window_on;

static void awake(void)
{
    pm_disable_tickless();
    wdg_guard_feed_hold(false);
    if (usb_off) {
        usb_off = false;
        usb_console_resume();
    }
}

void lp_window_begin(void)
{
    window_on = true;
    wdg_guard_feed_hold(true);
    pm_enable_tickless();
}

void lp_window_pause(bool pause)
{
    if (pause) {
        awake();
    } else if (window_on) {
        wdg_guard_feed_hold(true);
        pm_enable_tickless();
    }
}

void lp_window_end(void)
{
    window_on = false;
    awake();
}

static int lp_enter(void *arg)
{
    sl_entered++;
    /* LEAKCAM: the USB console (PR1 on the bench) and the watchdog stop for the sleep (tests/test120) */
    if (!usb_off) {
        usb_console_suspend();
        usb_off = true;
    }
    wdg_guard_pause(true);
    return 0;
}

/***********************************************************
    shell 
 ***********************************************************/
extern void vPortSetupTimerInterrupt(void);

static void set_cpu_bclk_80M_and_gate_clk(void)
{
    uint32_t tmpVal = 0;

    GLB_Set_MCU_System_CLK_Div(0, 3);
    CPU_Set_MTimer_CLK(ENABLE, BL_MTIMER_SOURCE_CLOCK_MCU_CLK, Clock_System_Clock_Get(BL_SYSTEM_CLOCK_MCU_CLK) / 1000000 - 1);

    /* clk gate,except DMA&CPU&UART0&SF&EMI&WIFI&EFUSE */
    tmpVal = 0;
    tmpVal = BL_SET_REG_BITS_VAL(tmpVal, GLB_CGEN_M_CPU, 1);
    tmpVal = BL_SET_REG_BITS_VAL(tmpVal, GLB_CGEN_M_DMA, 1);
    tmpVal = BL_SET_REG_BITS_VAL(tmpVal, GLB_CGEN_M_SEC, 1);
    tmpVal = BL_SET_REG_BITS_VAL(tmpVal, GLB_CGEN_M_SDU, 1);
    BL_WR_REG(GLB_BASE, GLB_CGEN_CFG0,tmpVal);

    tmpVal = 0;
    tmpVal = BL_SET_REG_BITS_VAL(tmpVal, GLB_CGEN_S1_EF_CTRL, 1);
    tmpVal = BL_SET_REG_BITS_VAL(tmpVal, GLB_CGEN_S1_SF_CTRL, 1);
    tmpVal = BL_SET_REG_BITS_VAL(tmpVal, GLB_CGEN_S1_DMA, 1);
    tmpVal = BL_SET_REG_BITS_VAL(tmpVal, GLB_CGEN_S1A_UART0, 1);
    tmpVal = BL_SET_REG_BITS_VAL(tmpVal, GLB_CGEN_S1A_UART1, 1);
    tmpVal = BL_SET_REG_BITS_VAL(tmpVal, GLB_CGEN_S1_SEC_ENG, 1);
    BL_WR_REG(GLB_BASE, GLB_CGEN_CFG1,tmpVal);

    tmpVal = 0;
    tmpVal = BL_SET_REG_BITS_VAL(tmpVal, GLB_CGEN_S2_WIFI, 1);
    tmpVal = BL_SET_REG_BITS_VAL(tmpVal, GLB_CGEN_S1_EXT_EMI_MISC, 1);
    tmpVal = BL_SET_REG_BITS_VAL(tmpVal, GLB_CGEN_S1_EXT_PIO, 1);

    BL_WR_REG(GLB_BASE, GLB_CGEN_CFG2, tmpVal);

    /* LEAKCAM: the USB console and the watchdog after a wake, as board_init() leaves them (CONFIG_BSP_USB, wdg_guard.c) */
    PERIPHERAL_CLOCK_USB_ENABLE();
    GLB_Set_USB_CLK_From_WIFIPLL(1);
    PERIPHERAL_CLOCK_TIMER0_1_WDG_ENABLE();
}


static int lp_exit(void *arg)
{
    int wakeup_reason;
    extern TaskHandle_t rxl_process_task_hd;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    set_cpu_bclk_80M_and_gate_clk();

    /* recovery system_clock_init\peripheral_clock_init\console_init*/
    board_recovery();

    //GLB_Set_EM_Sel(GLB_WRAM160KB_EM0KB);
    //bflb_sys_em_config();

#if defined(CFG_BLE_ENABLE) && defined(BL618DG)
    board_rf_ctl(BRD_CTL_RF_RESET_DEFAULT, 0);
    extern void cmd_set_btble_standalone(int argc, char **argv);
    // extern void cmd_set_btble_combo(int argc, char **argv);

    cmd_set_btble_standalone(0, NULL);
    // cmd_set_btble_combo(0, NULL);   
#endif
    vPortSetupTimerInterrupt();

    /* LEAKCAM: the shell is on USB (usb_console2.c), UART0 is the K230 link */
    wdg_guard_pause(false);

    wakeup_reason = bl_lp_get_wake_reason();

    /* not on LPFW_WAKEUP_BLE as in the example: on the BL616 every advertising event wakes the app, so the first
     * one ended the sleep mode (test120, 2026-11-01); a connection ends it (ble_connected) */

    if (wakeup_reason & LPFW_WAKEUP_WIFI_BROADCAST) {
        vTaskNotifyGiveFromISR(rxl_process_task_hd, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    } else {
        pm_alloc_mem_reset();
    }

    /* LEAKCAM: no JTAG remux of IO0-IO3: IO0 holds the K230 in reset, IO3 is USB PGOOD (firmware/bl616/board_pins.h) */
    return 0;
}

int bflb_pm_app_check(void)
{
    return pm_pbufc_check();
}

#ifdef CONFIG_SHELL
static void cmd_tickless(int argc, char **argv)
{	
    int broadcast = 0;

    if ((argc > 1) && (argv[1] != NULL)) {
        printf("%s\r\n", argv[1]);
        broadcast = atoi(argv[1]);
    } else {
        broadcast = 0;
    }

    if (broadcast) {
        enable_multicast_broadcast = 1;
    } else {
        enable_multicast_broadcast = 0;
    }

    pm_enable_tickless();
}

static void cmd_set_dtim(int argc, char **argv)
{
    int dtim = 10;

    if ((argc > 1) && (argv[1] != NULL)) {
        printf("%s\r\n", argv[1]);
        dtim = atoi(argv[1]);
    } else {
        dtim = 10;
    }
    
    set_dtim_config(dtim);
}

static int test_tcp_keepalive(int argc, char **argv)
{
    int sockfd;
    // uint8_t *recv_buffer;
    struct sockaddr_in dest, my_addr;
    char buffer[51];
    uint32_t pck_cnt = 0;
    uint32_t pck_total = 0;

    /* Create a socket */
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Error in socket\r\n");
        return -1;
    }

    /*---Initialize server address/port struct---*/
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons(50001);

    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(50001);
    inet_aton(argv[1], &dest.sin_addr);

    if (argc == 4) {
        pck_cnt = atoi(argv[3]);
        printf("keep alive pck:%ld\r\n");
    }

    printf("tcp server ip: %s\r\n", argv[1]);

    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr)) != 0) {
        printf("Error in bind\r\n");
        close(sockfd);
        return -1;
    }

    /*---Connect to server---*/
    if (connect(sockfd, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        printf("Error in connect\r\n");
        close(sockfd);
        return -1;
    }

    /*---Get "Hello?"---*/
    memset(buffer, 'A', sizeof(buffer) - 1);

#ifdef CONFIG_LPAPP
    if (argc > 2) {
        cmd_tickless(0, NULL);
    }
#endif

    int ret = 0;

    while (1) {
        pck_total++;
        snprintf(buffer, sizeof(buffer), "SEQ = %ld  ", pck_total);

        buffer[sizeof(buffer) - 2] = '\n';
        ret = write(sockfd, buffer, sizeof(buffer) - 1);
        if (ret != sizeof(buffer) - 1) {
            printf("write error: %d\n", ret);
            break;
        }
        printf("**********************************\n");
        printf("SEQ:%ld WRITE SUCCESS %d\n", pck_total, ret);

        if (pck_cnt && (pck_total >= pck_cnt)) {
            bl_pm_event_bit_set(PSM_EVENT_APP);
            break;
        }
#if 0
        ret = read(sockfd, buffer, sizeof(buffer)-1);
        buffer[sizeof(buffer) -1] = 0;
        printf("read ret: %d, %s\r\n", ret, buffer);
#endif
        vTaskDelay(pdMS_TO_TICKS(30 * 1000));
    }

    close(sockfd);
    return 0;
}

static void cmd_hbn_test(int argc, char **argv)
{
#if defined(BL618DG)
    (void)argc;
    (void)argv;

    printf("hbn_test unsupported on BL618DG wl_ble_lp\r\n");
#else
    if (argc <= 5) {
        bl_lp_hbn_fw_cfg_t hbn_test_cfg = {
            .hbn_sleep_cnt = 32768 * 5,
            .hbn_level = 0,
        };
        bl_lp_hbn_enter(&hbn_test_cfg);
    } else {
        if ((argv[1] != NULL) && (argv[2] != NULL) && (argv[3] != NULL)) {
            printf("wdt_pin %d\r\n", atoi(argv[1]));
            printf("max_continue_times %d\r\n", atoi(argv[3]));
            bl_lp_hbn_init(1, atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
        }

        if ((argv[4] != NULL) && (argv[5] != NULL)) {
            printf("hbn sleep %ds\r\n", atoi(argv[4]));
            printf("hbn_level %d\r\n", atoi(argv[5]));
            vTaskDelay(1);
            bl_lp_hbn_fw_cfg_t hbn_test_cfg = {
                .hbn_sleep_cnt = 32768 * atoi(argv[4]),
                .hbn_level = atoi(argv[5]),
            };
            bl_lp_hbn_enter(&hbn_test_cfg);
        }
    }
#endif
}

static void cmd_io_dbg(int argc, char **argv)
{
    if (argc != 2) {
        printf("cmd_io_dbg err\r\n");
        return;
    }

    if (atoi(argv[1]) <= 34) {
#if defined(BL618DG)
        iot2lp_para->wifi_debug_io = atoi(argv[1]);
#else
        iot2lp_para->debug_io = atoi(argv[1]);
#endif
    } else {
#if defined(BL618DG)
        iot2lp_para->wifi_debug_io = 0xFF;
#else
        iot2lp_para->debug_io = 0xFF;
#endif
    }
}

#if defined(CONFIG_CLOCK_SOURCE_EF_PARAM) && CONFIG_CLOCK_SOURCE_EF_PARAM
static void cmd_set_clock_source(int argc, char **argv)
{
    uint8_t source;

    // Check argument count
    if (argc != 2) {
        printf("Usage: set_clock_source <source>\r\n");
        printf("  1: Internal RC oscillator\r\n");
        printf("  2: External passive crystal\r\n");
        printf("  3: External active crystal\r\n");
        return;
    }

    // Parse source value from argument
    source = (uint8_t)atoi(argv[1]);

    // Validate source value
    if (source < 1 || source > 3) {
        printf("Error: Invalid clock source value. Must be 1, 2, or 3.\r\n");
        return;
    }

    // Call the API function to set clock source
    int ret = app_set_clock_source(source);
    if (ret == 0) {
        printf("Clock source updated to %d.\r\n", source);
        // The system should reboot automatically after this
    } else {
        printf("Failed to set clock source, error code: %d\r\n", ret);
    }
}

static void cmd_get_clock_source(int argc, char **argv)
{
    uint8_t source;
    int ret;

    // Call the API function to get current clock source
    ret = app_get_clock_source(&source);

    if (ret == 0) {
        printf("Current clock source: %d (", source);

        // Print descriptive name of the clock source
        switch (source) {
            case 1:
                printf("Internal RC oscillator");
                break;
            case 2:
                printf("External passive crystal");
                break;
            case 3:
                printf("External active crystal");
                break;
            default:
                printf("Unknown");
                break;
        }
        printf(")\r\n");
    } else {
        printf("Failed to get clock source, error code: %d\r\n", ret);
        printf("Using default source: Internal RC oscillator\r\n");
    }
}
#endif

#if !defined(BL616)
static void cmd_lpfw_uart_cfg(int argc, char **argv)
{
    if (argc != 5) {
        printf("Need param\r\n");
        return;
    }

    iot2lp_para->uart_config->debug_log_en = atoi(argv[1]);
    iot2lp_para->uart_config->uart_tx_io = atoi(argv[2]);
    iot2lp_para->uart_config->uart_rx_io = atoi(argv[3]);
    iot2lp_para->uart_config->baudrate = atoi(argv[4]);
}

static void cmd_lpfw_clock_cfg(int argc, char **argv)
{
    if (argc != 5) {
        printf("Need param\r\n");
        printf("mcu_clk_sel:\n\t0:GLB_MCU_SYS_CLK_RC32M\r");
        printf("\n\t1:GLB_MCU_SYS_CLK_XTAL\r");
        printf("\n\t2:GLB_MCU_SYS_CLK_WIFIPLL_96M\r");
        printf("\n\t3:GLB_MCU_SYS_CLK_WIFIPLL_192M\r");
        printf("\n\t4:GLB_MCU_SYS_CLK_TOP_WIFIPLL_240M\r");
        printf("\n\t5:GLB_MCU_SYS_CLK_TOP_WIFIPLL_320M\r\n");

        printf("xclk_sel:\n\t0:HBN_MCU_XCLK_RC32M\r");
        printf("\n\t1:HBN_MCU_XCLK_XTAL\r");

        return;
    }

    iot2lp_para->clock_config->mcu_clk_sel = atoi(argv[1]);
    iot2lp_para->clock_config->hclk_div = atoi(argv[2]);
    iot2lp_para->clock_config->bclk_div = atoi(argv[3]);
    iot2lp_para->clock_config->xclk_sel = atoi(argv[4]);
}
#endif

SHELL_CMD_EXPORT_ALIAS(cmd_tickless, pm_enter_lp, cmd tickless);
SHELL_CMD_EXPORT_ALIAS(cmd_set_dtim, wifi_lp_set_dtim, cmd_set_dtim);
SHELL_CMD_EXPORT_ALIAS(test_tcp_keepalive, lpfw_tcp_keepalive, tcp keepalive test);
SHELL_CMD_EXPORT_ALIAS(cmd_hbn_test, hbn_test, hbn test);
SHELL_CMD_EXPORT_ALIAS(cmd_io_dbg, io_debug, cmd io_debug);
#if defined(CONFIG_CLOCK_SOURCE_EF_PARAM) && CONFIG_CLOCK_SOURCE_EF_PARAM
SHELL_CMD_EXPORT_ALIAS(cmd_set_clock_source, set_clock_source, Set system clock source (1:RC, 2:Passive XTAL, 3:Active XTAL));
SHELL_CMD_EXPORT_ALIAS(cmd_get_clock_source, get_clock_source, Get current system clock source);
#endif
#if !defined(BL616)
SHELL_CMD_EXPORT_ALIAS(cmd_lpfw_uart_cfg, lpfw_uart, cmd lpfw_uart);
SHELL_CMD_EXPORT_ALIAS(cmd_lpfw_clock_cfg, lpfw_clock, cmd lpfw_clock);
#endif
#endif
/* pt_ble.c's helpers (tests.h): RF and easyflash come from `lp_boot` */
static volatile bool lp_booted;

bool pt_radio_init(void)
{
    if (!lp_booted)
        printf("pt_ble: run lp_boot first\r\n");
    return lp_booted;
}

int pt_ef_once(void)
{
    return lp_booted ? 0 : -1;
}

/* ble_pairing.h: new credentials stored; the setup firmware switches to the operational slot here once presence works
 * (DESIGN.md 5.3.1), this test only says so */
void ble_credentials_stored(void)
{
    printf("lp: credentials stored\r\n");
}

/* BQ24072 PGOOD, low = USB power (firmware/bl616/board_pins.h); pt_ble.c prints it */
bool usb_present(void)
{
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    bflb_gpio_init(gpio, PIN_USB_PGOOD, GPIO_INPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_0);
    return !bflb_gpio_read(gpio, PIN_USB_PGOOD);
}

static void lp_boot_task(void *arg)
{
    (void)arg;
#if !defined(BL618DG)
    uint8_t soc_v, rt_v, aon_v;
#endif

    printf("lp: boot steps\r\n");

#if !defined(BL618DG)
    hal_pm_ldo11_cfg(PM_PDS_LDO_LEVEL_SOC_DEFAULT, PM_PDS_LDO_LEVEL_RT_DEFAULT, PM_PDS_LDO_LEVEL_AON_DEFAULT);
    hal_pm_ldo11_cfg_get(&soc_v, &rt_v, &aon_v);
    printf("SOC:%d RT:%d AON:%d\r\n", soc_v, rt_v, aon_v);
#endif

    lp_crumb(4);
    HBN_Enable_RTC_Counter();
#if !defined(BL618DG)
    pm_rc32k_auto_cal_init();
#endif

    lp_crumb(5);
    if (bflb_boot2_init() != 0 || easyflash_init() != EF_NO_ERR)
        vAssertCalled();   /* the partition table and easyflash, checked (firmware/bl616/provision.c) */

    /* RC32K first. The example stores the passive crystal (Y3) and resets the chip, and app_clock_init() falls back to
     * RC and stores that when its crystal check fails: a reset loop with a flash write per boot (suspected for the first
     * build, which never enumerated). Y3 needs the HBN pulls off and a start-up wait (test032); trying it is a later
     * test120 step. A stored crystal setting is replaced once (one reset). */
    lp_crumb(6);
    if (app_set_clock_source(CLOCK_SOURCE_RC) != 0)
        vAssertCalled();

    lp_crumb(7);
    pm_sys_init();

#ifdef CONFIG_LPAPP
    lp_crumb(8);
    bl_lp_init(); //wifi lowpower
    bl_lp_sys_callback_register(lp_enter, NULL, lp_exit, NULL);
#endif

    /* set ble controller EM Size */
#ifdef CFG_BLE_ENABLE
extern int bl_sys_em_config(void);
    bl_sys_em_config();
#endif

    lp_crumb(9);
    if (rfparam_init(0, NULL, 0) != 0)
        vAssertCalled();   /* fail fast: no radio, nothing to test */
    printf("lp: rf up\r\n");

    lp_crumb(10);
    tcpip_init(NULL, NULL);

    xTaskCreate(wifi_start_firmware_task, "wifi init", 1024, NULL, 27, NULL);
    xTaskCreate(bluetooth_start_task, (char *)"bluetooth_start", 1024, NULL, 15, &bluetooth_start_handle);
    lp_booted = true;
    lp_crumb(11);
    printf("lp: boot done\r\n");
    vTaskDelete(NULL);
}

static void cmd_lp_boot(int argc, char **argv)
{
    static bool started;
    if (started) {
        printf("lp_boot: already run\r\n");
        return;
    }
    started = true;
    if (xTaskCreate(lp_boot_task, "lp_boot", 1024, NULL, 10, NULL) != pdPASS)
        vAssertCalled();
}
SHELL_CMD_EXPORT_ALIAS(cmd_lp_boot, lp_boot, the low-power bring-up steps (test120));

static void cmd_lp_crumb(int argc, char **argv)
{
    if (LP_CRUMB[0] != 0x3142504cu)
        printf("lp_crumb: none\r\n");
    else
        printf("lp_crumb: last step %lu, %lu marks\r\n", (unsigned long)LP_CRUMB[1], (unsigned long)LP_CRUMB[2]);
}
SHELL_CMD_EXPORT_ALIAS(cmd_lp_crumb, lp_crumb, the last low-power bring-up step (kept across a watchdog reset));

static void cmd_lp_sleepstat(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("sleep hook calls %lu, idle <= %lu ms %lu, max idle %lu ms, sleeps entered %lu, BLE idle vetoes %lu, pm event %d, sleep checks %d\r\n",
           (unsigned long)sl_calls, (unsigned long)configEXPECTED_IDLE_TIME_BEFORE_SLEEP, (unsigned long)sl_short,
           (unsigned long)sl_max, (unsigned long)sl_entered, (unsigned long)sl_vetoed, (int)bl_pm_event_get(),
           pm_sleep_check_dispatch());
}
SHELL_CMD_EXPORT_ALIAS(cmd_lp_sleepstat, lp_sleepstat, test120: tickless sleep attempts and entries);

/* the BLE core's deep-sleep control (DEEPSLCNTL, 0x28000030): after PDS sleep with BLE idle (no advertising, so no
 * wake deadline) and a software reset, the controller's init spun forever in rwip_time_get (test120, 2026-11-01): the
 * core stays in deep sleep across the reset. `lp_blecore` prints it, `lp_blecore wake` sets SOFT_WAKEUP_REQ (bit 15) */
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
        /* a reset of the BLE core alone, not the whole PDS block (that one drops USB) */
        GLB_AHB_MCU_Software_Reset(argv[1][1] == 't' ? GLB_AHB_MCU_SW_BTDM : GLB_AHB_MCU_SW_BLE2);
        printf("lp_blecore: %s reset, DEEPSLCNTL %08lx, 0x28000100 %08lx\r\n", argv[1],
               (unsigned long)*BLE_DEEPSLCNTL, (unsigned long)*(volatile uint32_t *)0x28000100);
    }
}
SHELL_CMD_EXPORT_ALIAS(cmd_lp_blecore, lp_blecore, test120: BLE core deep-sleep control [wake | btdm | ble2]);

/* the PDS block's power control (persists across a software reset): PDS_CTL, CTL2 (per-domain force bits: WB = Wi-Fi
 * and BLE, USB), CTL3, CTL4, CTL5 */
static void cmd_lp_pds(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "restore")) {
        /* experiment: the values a freshly reset chip has (2026-11-01), over what a PDS sleep with BLE idle left */
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

/* which reset the firmware can trigger clears the wedged BLE core (test120): wdg = hang with interrupts off until the
 * watchdog resets the chip (WDG_GUARD_MS), por / sys / cpu = GLB_SW_POR_Reset / _System_Reset / _CPU_Reset */
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
    /* HBN RAM rule (tests/test021): retention/sleep mode off before the first HBN RAM access (firmware/bl616/main.c) */
    BL_WR_REG(HBN_BASE, HBN_SRAM, BL_RD_REG(HBN_BASE, HBN_SRAM) & ~(HBN_RETRAM_RET_MSK | HBN_RETRAM_SLP_MSK));
    board_init();          /* also the USB CDC console and its shell (defconfig) */
    crash_rec_report();
    (void)wdg_guard_start();

    vTaskStartScheduler();

    while (1) {
    }
}
