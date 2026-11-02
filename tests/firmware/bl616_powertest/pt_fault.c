/*
 * test110: loss injected into the product's provisioning (firmware/bl616_setup/ble_pairing.c + wifi_check.c), so the
 * phone app's recovery is tested at every step. Linker wraps (CMakeLists.txt): the product sources stay unchanged.
 * BLE retransmits on the link layer, so loss shows there as a dropped notification (host buffers full) or a dropped
 * link (supervision timeout); Wi-Fi loss as a DNS answer or an HTTP answer that never comes.
 *
 *   pt_fault                   the armed fault and how often it fired
 *   pt_fault off
 *   pt_fault notify all|half|final    STATUS notifications dropped: every one, every second one, the result only
 *   pt_fault link connect|joining|address|internet|result
 *                              the link dropped right after connect, or when STATUS reaches that state (result: SAVED
 *                              or a failure); that state's notification is lost with it
 *   pt_fault dns|http          the check's DNS lookup fails / its HTTP answer never comes
 */
#include <stdio.h>
#include <string.h>

#include "bluetooth.h"
#include "conn.h"
#include "gatt.h"
#include "hci_driver.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "shell.h"

enum mode { OFF, N_ALL, N_HALF, N_FINAL, L_CONNECT, L_JOINING, L_ADDRESS, L_INTERNET, L_RESULT, W_DNS, W_HTTP };
static const char *const names[] = { "off", "notify all", "notify half", "notify final", "link connect",
                                     "link joining", "link address", "link internet", "link result", "dns", "http" };
static volatile enum mode mode;
static volatile unsigned fired, seen;

int __real_bt_gatt_notify_cb(struct bt_conn *conn, struct bt_gatt_notify_params *params);
int __real_bt_conn_set_security(struct bt_conn *conn, bt_security_t sec);
int __real_lwip_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                            struct addrinfo **res);
ssize_t __real_lwip_recv(int s, void *mem, size_t len, int flags);

/* STATUS states (wifi_check_calc.h): 0x01 joining, 0x02 address, 0x03 internet, 0x04 saved, >= 0x10 failures */
static bool link_drop_at(uint8_t st)
{
    return (mode == L_JOINING && st == 0x01) || (mode == L_ADDRESS && st == 0x02) ||
           (mode == L_INTERNET && st == 0x03) || (mode == L_RESULT && st >= 0x04);
}

int __wrap_bt_gatt_notify_cb(struct bt_conn *conn, struct bt_gatt_notify_params *params)
{
    uint8_t st = params->len ? ((const uint8_t *)params->data)[0] : 0;
    bool drop = mode == N_ALL || (mode == N_HALF && (seen++ & 1)) || (mode == N_FINAL && st >= 0x04);
    if (link_drop_at(st)) {
        fired++;
        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        return 0;
    }
    if (drop) {
        fired++;
        return 0;                                /* "sent": a lost notification looks like this to the sender */
    }
    return __real_bt_gatt_notify_cb(conn, params);
}

int __wrap_bt_conn_set_security(struct bt_conn *conn, bt_security_t sec)
{
    if (mode == L_CONNECT) {                     /* ble_pairing.c asks for encryption right after connect */
        fired++;
        return bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    }
    return __real_bt_conn_set_security(conn, sec);
}

int __wrap_lwip_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                            struct addrinfo **res)
{
    if (mode == W_DNS) {
        fired++;
        return EAI_FAIL;
    }
    return __real_lwip_getaddrinfo(node, service, hints, res);
}

ssize_t __wrap_lwip_recv(int s, void *mem, size_t len, int flags)
{
    if (mode == W_HTTP) {
        fired++;
        return -1;                               /* the receive timeout ran out */
    }
    return __real_lwip_recv(s, mem, len, flags);
}

static void cmd_fault(int argc, char **argv)
{
    if (argc > 1) {
        char want[24];
        snprintf(want, sizeof(want), "%s%s%s", argv[1], argc > 2 ? " " : "", argc > 2 ? argv[2] : "");
        unsigned i = 0;
        while (i < sizeof(names) / sizeof(names[0]) && strcmp(names[i], want))
            i++;
        if (i == sizeof(names) / sizeof(names[0])) {
            printf("pt_fault: unknown \"%s\"\r\n", want);
            return;
        }
        mode = (enum mode)i;
        fired = seen = 0;
    }
    printf("pt_fault: %s, fired %u\r\n", names[mode], fired);
}
SHELL_CMD_EXPORT_ALIAS(cmd_fault, pt_fault, test110 loss injection: off | notify all|half|final | link <state> | dns | http);
