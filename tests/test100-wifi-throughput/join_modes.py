#!/usr/bin/env python3
"""test100 (2026-10-25): BL616-alone join time by connect mode - full scan (as shipped), quick scan (first AP with
the SSID), remembered (the last AP's channel + BSSID, no scan); 3 joins each, a leave between. Credentials from
~/.leakcam_wifi (board.wifi_credentials), never printed."""
import os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "power-bringup"))
from board import bl, log, wifi_credentials   # noqa: E402

ssid, psk = wifi_credentials()
bl("pt_wifi scan", 15, r"bouffalolab />[^\n]*$")          # radio up
for mode in ("", "quick", "remembered", "", "quick", "remembered", "", "quick", "remembered"):
    out = bl(f'pt_wifi join "{ssid}" "{psk}" {mode}'.strip(), 30, r"join to address[^\n]*\n|no DHCP|not associated|refused")
    out = out.replace(psk, "***")
    a = re.search(r"associated \+ WPA in (\d+) ms \(([^)]*)\)", out); d = re.search(r"DHCP in (\d+) ms; join to address (\d+) ms", out)
    log(f"{mode or 'full':6s}", f"assoc+WPA {a.group(1) if a else '?'} ms ({a.group(2) if a else '-'})",
        f"DHCP {d.group(1) if d else '?'} ms, total {d.group(2) if d else '?'} ms" if d else re.findall(r"pt_wifi join: [^\n]*", out)[-1:])
    bl("pt_wifi leave", 5, r"bouffalolab />[^\n]*$")
