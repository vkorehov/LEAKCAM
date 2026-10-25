#!/usr/bin/env python3
"""test100 (2026-10-25): the product's own join (wifi_link.c join_stored at the bridge's start, stored credentials):
time to associated + WPA, first with no remembered AP (quick scan), then with it (no scan). Credentials from
~/.leakcam_wifi, never printed."""
import os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup"))
from board import bl, log, wifi_credentials   # noqa: E402

ssid, psk = wifi_credentials()
print(bl(f'pt_wifi store "{ssid}" "{psk}"', 3).replace(psk, "***").strip().splitlines()[-2:])
print(bl("pt_wifi forget", 3).strip().splitlines()[-2:])
for i in range(4):
    bl("pt_off", 2); bl("pt_on", 2)
    out = bl("pt_wifi bridge", 25, r"after wifi_link_start[^\n]*\n|radio busy|rails off")
    m = re.search(r"(associated \+ WPA|not associated) (\d+) ms after wifi_link_start", out)
    log(f"bridge start {i + 1} ({'no remembered AP' if i == 0 else 'remembered AP'}):", m.group(1) if m else "?", m.group(2) if m else "?", "ms")
bl("pt_off", 2)
