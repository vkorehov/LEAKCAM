#!/usr/bin/env python3
"""LEAKCAM changes to bouffalo_sdk (2.3.35), applied before every BL616 build by host.mk (product and test firmware).
Idempotent: each change carries a "LEAKCAM:" marker and is applied once; a changed SDK fails loudly instead of
building without it.

  NetHub SDIO download buffers 15 -> 26 and the netdev's download credits 8 -> 23 (BL618DG's figures): with 8 credits
  the K230 waited for the BL616 most of the time (tests/test100, 2026-10-25: TCP up 19.4 -> 27.5 Mbit/s, UDP 21.5 ->
  35.4; +22 KB BL616 RAM, 155 KB still free)

    sdk_patch.py <bouffalo_sdk dir>
"""
import os
import sys

SDIO = "components/net/nethub/backend/host/sdio/"
PATCHES = [
    (SDIO + "msg_ctrl_sdio.h",
     "#else\n#define MSG_DNLD_FRAME_CNT (15)\n",
     "#else\n#define MSG_DNLD_FRAME_CNT (26)   /* LEAKCAM: was 15 (tests/test100 throughput) */\n"),
    (SDIO + "netdev_wifi.c",
     "#else\n        .dnld_credit_max = 8,\n",
     "#else\n        .dnld_credit_max = 23,      /* LEAKCAM: was 8 (tests/test100 throughput) */\n"),
]

sdk = sys.argv[1]
for rel, old, new in PATCHES:
    p = os.path.join(sdk, rel)
    s = open(p).read()
    if new in s:
        continue
    if s.count(old) != 1:
        sys.exit(f"sdk_patch.py: {rel} changed, cannot apply: {old.strip()!r}")
    open(p, "w").write(s.replace(old, new))
    print(f"sdk_patch.py: patched {rel}")
