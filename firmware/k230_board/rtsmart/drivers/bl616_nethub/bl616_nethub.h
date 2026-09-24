/*
 * LEAKCAM: Wi-Fi through the BL616 (Ai-M62-CBS) on K230 MMC0, as an SDIO function-1 device.
 *
 * The BL616 runs Bouffalo's NetHub: its SDIO device unit (SDU) moves whole messages through
 * a small ring of "ports", and a message router on top multiplexes Ethernet frames and
 * control messages by tag. The BL616 is only the radio: the K230's lwIP owns the IP address
 * (DHCP), ARP, ICMP and every socket, using the BL616's station MAC.
 *
 *   bl616_sdio.c   SDIO probe, the SDU handshake, one message in or out through a port
 *   bl616_msg.c    message router: channel handshake, download credits, TX queues, worker
 *   bl616_wifi.c   RT-Thread WLAN device (lwIP netif "wlan0") and the control protocol
 *
 * The wire facts below were read from the BL616 side of Bouffalo's SDK (Apache-2.0:
 * drivers/lhal/src/bflb_sdio2.c, bsp/common/msg_router/device) and cross-checked against the
 * register names of their Linux host driver. No code is taken from the Linux driver.
 */
#ifndef LEAKCAM_BL616_NETHUB_H
#define LEAKCAM_BL616_NETHUB_H

#include <rtthread.h>

/* SDIO IDs of the BL616 SDU function 1 */
#define BL616_SDIO_VENDOR      0x424c
#define BL616_SDIO_DEVICE      0x0606

/*
 * SDU function-1 registers (CMD52 addresses). The BL616 sees the same registers at +0x100.
 */
#define BL616_REG_HOST_INT_MASK   0x02  /* bit 0: interrupt the host when an upload is queued */
#define BL616_REG_HOST_INT_STATUS 0x03  /* writing 0 clears it and releases DAT1 */
#define BL616_REG_RD_BITMAP       0x04  /* 16 bit: upload port n holds a message */
#define BL616_REG_WR_BITMAP       0x06  /* 16 bit: download port n has a free BL616 buffer */
#define BL616_REG_RD_LEN(port)    (0x08 + 2 * (port))  /* length of the message in port n */
#define BL616_REG_HOST_READY      0x60  /* host writes its upload buffer size / 64 */
#define BL616_REG_CARD_READY      0x61  /* BL616 writes its download buffer size / 64 */
#define BL616_REG_STATUS          0x62  /* flags below */
#define BL616_REG_OUT_PTR         0x63  /* next download port << 4 | next upload port */
#define BL616_REG_IO_PORT         0x78  /* 24 bit CMD53 address of port 0 */

#define BL616_STATUS_NOT_SDIO_BOOT (1u << 7)
#define BL616_STATUS_APP_RUN       (1u << 6)
#define BL616_STATUS_RD_LEN_PACKED (1u << 5)  /* RD_LEN uses the packed form, see bl616_sdio.c */

#define BL616_SIZE_UNIT        64    /* unit of HOST_READY / CARD_READY */
#define BL616_BLOCK_SIZE       512
/* ports per direction: SDIO2_MAX_PORT_NUM on the BL616, which NetHub builds with
 * CONFIG_MR_SDIO_QUEUE_DEPTH (4, set in firmware/bl616_wifi/defconfig). Both ends count the
 * ports in the same order, so the two numbers must be equal; there is no register for it. */
#define BL616_PORTS            4

/* our upload (BL616 -> K230) buffer, announced in HOST_READY. The largest upload is an
 * Ethernet frame (1514) plus the 8-byte channel header, padded to 3 blocks = 1536. */
#define BL616_UPLD_MAX         2048

/*
 * Message router. Every SDIO message starts with this header; len counts the bytes after it.
 */
struct bl616_mr_hdr
{
    rt_uint8_t tag;
    rt_uint8_t sub_tag;
    rt_uint16_t len;
} __attribute__((packed));

#define BL616_TAG_NETDEV_STA   5     /* Ethernet frames of the station interface */
#define BL616_TAG_VCHAN        13    /* virtual channel: our control messages (USER type) */

/*
 * Both NetHub channels (netdev and virtual channel) put this header behind the router header
 * and run the same handshake:
 *   K230 HOST_READY -> BL616 DEVICE_START (+ initial download credit), then data both ways.
 *   BL616 DEVICE_RESET or DEVICE_STOP -> the K230 starts over with HOST_READY.
 * Download credit: the BL616 tells the absolute number (mod 256) of DNLD_DATA messages it
 * can take in credit_limit whenever credit_update is set; the K230 may send while its own
 * count of DNLD_DATA messages sent since DEVICE_START is behind it.
 */
struct bl616_ch_hdr
{
    struct bl616_mr_hdr mr;
    rt_uint8_t reserved;
    rt_uint8_t flag;
    rt_uint8_t credit_update;
    rt_uint8_t credit_limit;
} __attribute__((packed));

enum
{
    BL616_FLAG_DNLD_DATA     = 1,
    BL616_FLAG_UPLD_DATA     = 2,
    BL616_FLAG_HOST_RESET    = 3,
    BL616_FLAG_HOST_READY    = 4,
    BL616_FLAG_HOST_STOP     = 5,
    BL616_FLAG_DEVICE_RESET  = 6,
    BL616_FLAG_DEVICE_START  = 7,   /* netdev: payload = MAC[6] + IPv4[4] of the BL616 */
    BL616_FLAG_DEVICE_STOP   = 8,
    BL616_FLAG_CREDIT_UPDATE = 9,
    BL616_FLAG_MAC_IP_UPDATE = 10,  /* netdev: payload = MAC[6] + IPv4[4] */
};

/* virtual channel payload header (nethub_vchan_data_hdr_t) */
struct bl616_vchan_hdr
{
    rt_uint8_t type;
    rt_uint8_t reserved;
    rt_uint16_t len;
} __attribute__((packed));

#define BL616_VCHAN_USER       1

/* bl616_sdio.c: one message per call; both take the MMC host lock themselves */
rt_err_t bl616_sdio_read(rt_uint8_t *buf, rt_size_t size, rt_size_t *len);
rt_err_t bl616_sdio_write(const rt_uint8_t *buf, rt_size_t len);
rt_size_t bl616_sdio_dnld_max(void);

/* bl616_msg.c */
rt_err_t bl616_link_start(void);
void bl616_link_stop(void);
void bl616_link_wake(void);
rt_err_t bl616_link_send_frame(const void *frame, rt_size_t len);
rt_err_t bl616_link_send_ctrl(const void *msg, rt_size_t len);

/* bl616_wifi.c, called from the link worker */
void bl616_wifi_attach(const rt_uint8_t mac[6]);
void bl616_wifi_ctrl_up(void);
void bl616_wifi_rx(void *frame, rt_size_t len);
void bl616_wifi_ctrl_rx(const rt_uint8_t *msg, rt_size_t len);
void bl616_wifi_detach(void);

#endif
