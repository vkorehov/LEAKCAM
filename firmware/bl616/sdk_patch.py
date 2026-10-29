#!/usr/bin/env python3
"""LEAKCAM changes to bouffalo_sdk (2.3.35), applied before every BL616 build by host.mk (product and test firmware).
Idempotent: each change carries a "LEAKCAM:" marker and is applied once; a changed SDK fails loudly instead of
building without it.

  NetHub SDIO download buffers 15 -> 26 and the netdev's download credits 8 -> 23 (BL618DG's figures): with 8 credits
  the K230 waited for the BL616 most of the time (tests/test100, 2026-10-25: TCP up 19.4 -> 27.5 Mbit/s, UDP 21.5 ->
  35.4; +22 KB BL616 RAM, 155 KB still free)

  The leftover Wi-Fi RAM becomes a heap ("WRAM") only when it holds TLSF's ~3.2 KB control block: with BLE (32 KB
  exchange memory) and the 26 buffers 576 B are left, TLSF built its control block over the start of the BLE exchange
  memory and the heap's size wrapped; `mem` walked it forever and the watchdog reset the chip (tests/test040,
  2026-10-28)

  LE Secure Connections on: the ble1m0s1bredr0 profile forces CONFIG_DISABLE_BT_ECC, which compiles out the host's
  public-key and DH-key handlers while the host still offers SC (the controller has the P-256 commands); Android then
  pairs with SC and the BL616 failed it ("Unhandled SMP code 0x0c", reason 5, tests/test040, 2026-10-28). Legacy
  Just Works is no way out: the passphrase crosses that link (DESIGN.md 5.3)

  No link key on the console: the host printed every LTK (BT_WARN "BLE LTK: ..."), with it anyone at the console or a
  log can decrypt the link the passphrase crosses (tests/test040, 2026-10-28)

  No Wi-Fi passphrase or session keys on the console: every join printed the supplicant's network config line by
  line, `Exec psk "<passphrase>"` among them (an unconditional printf in the prebuilt fhost library: its format is
  blanked, same length), and every connect and disconnect dumped the PTK/GTK: mm_sec_keydump() in the prebuilt MAC
  library (called from its own sm.c, inside LTO: -Wl,--wrap does not reach it) formats each key row and logs it as
  "%s" through wifi_syslog, which drops those rows (tests/test040, 2026-10-29)

    sdk_patch.py <bouffalo_sdk dir>
"""
import os
import sys

SDIO = "components/net/nethub/backend/host/sdio/"
WRAM = '    mm_register_heap(MM_HEAP_WRAM_0, "WRAM", MM_ALLOCATOR_TLSF, &_heap_wifi_start, (uintptr_t)&_heap_wifi_size);\n'
HOST = "components/wireless/bluetooth/blestack/src/host/"
PATCHES = [
    (SDIO + "msg_ctrl_sdio.h",
     "#else\n#define MSG_DNLD_FRAME_CNT (15)\n",
     "#else\n#define MSG_DNLD_FRAME_CNT (26)   /* LEAKCAM: was 15 (tests/test100 throughput) */\n"),
    (SDIO + "netdev_wifi.c",
     "#else\n        .dnld_credit_max = 8,\n",
     "#else\n        .dnld_credit_max = 23,      /* LEAKCAM: was 8 (tests/test100 throughput) */\n"),
    ("bsp/board/bl616dk/board.c", WRAM,
     "    if ((uintptr_t)&_heap_wifi_size >= 8192)   /* LEAKCAM: TLSF needs ~3.2 KB of it (tests/test040) */\n    "
     + WRAM),
    ("components/wireless/bluetooth/ble_common_readonly.cmake",
     "\t\t_check_conflict(CONFIG_BLE_ACT_MAX 7)\n\t\t_check_conflict(CONFIG_DISABLE_BT_ECC y)\n\t\t_check_conflict(CONFIG_ADV_EXTENSION n)\n",
     "\t\t_check_conflict(CONFIG_BLE_ACT_MAX 7)\n\t\t_check_conflict(CONFIG_DISABLE_BT_ECC n)   # LEAKCAM: LE Secure Connections (tests/test040)\n\t\t_check_conflict(CONFIG_ADV_EXTENSION n)\n"),
    (HOST + "conn.c",
     '\tBT_WARN("BLE LTK: %s\\r\\n",bt_hex(ltk,len));\n',
     "\t/* LEAKCAM: no LTK on the console (tests/test040) */\n"),
    (HOST + "hci_core.c",
     '\t\tBT_WARN("BLE LTK: %s\\r\\n",bt_hex(ltk,BT_SMP_MAX_ENC_KEY_SIZE));\n',
     "\t\t/* LEAKCAM: no LTK on the console (tests/test040) */\n"),
    ("components/wireless/macsw_plat.c",
     "void wifi_syslog(int priority, const char *fmt, ...)\n{\n",
     "void wifi_syslog(int priority, const char *fmt, ...)\n{\n"
     "    /* LEAKCAM: no session keys on the console (tests/test040): mm_sec_keydump() logs each key row as \"%s\"\n"
     "     * between its \"Keyram Dump\" and \"MAC Address\" headers */\n"
     "    static bool keyrows;\n"
     "    if (strstr(fmt, \"Keyram Dump\"))\n"
     "        keyrows = true;\n"
     "    else if (strstr(fmt, \"=  MAC Address =\"))\n"
     "        keyrows = false;\n"
     "    else if (keyrows && !strcmp(fmt, \"%s\"))\n"
     "        return;\n"),
]
# binary files: the same length, so nothing else in them moves
BINARY = [
    ("components/wireless/wifi6/fhost/lib/libfhost_bl616_default.a", b"\0\0Exec %s\r\n\0", b"\0\0\0xec %s\r\n\0"),
]

sdk = sys.argv[1]
for rel, old, new in BINARY:
    p = os.path.join(sdk, rel)
    s = open(p, "rb").read()
    if new in s:
        continue
    if s.count(old) != 1:
        sys.exit(f"sdk_patch.py: {rel} changed, cannot apply: {old!r}")
    open(p, "wb").write(s.replace(old, new))
    print(f"sdk_patch.py: patched {rel}")
for rel, old, new in PATCHES:
    p = os.path.join(sdk, rel)
    s = open(p).read()
    if new in s:
        continue
    if s.count(old) != 1:
        sys.exit(f"sdk_patch.py: {rel} changed, cannot apply: {old.strip()!r}")
    open(p, "w").write(s.replace(old, new))
    print(f"sdk_patch.py: patched {rel}")
