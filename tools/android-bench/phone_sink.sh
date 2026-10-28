#!/bin/sh
# The bench phone (test040's BLE driver) must charge from the computer, never power it. USB-C role negotiation on
# this setup makes the phone the power source again after reconnects (2026-10-28: twice, once down to 3 %), so this
# watches adb and swaps each phone that comes up as source to sink + device. Run it in the background during bench
# work, or as the systemd user service phone-sink.service (README.txt).
adb start-server >/dev/null 2>&1
adb track-devices 2>/dev/null | while read -r _; do
    sleep 2
    for d in $(adb devices | awk 'NR > 1 && $2 == "device" { print $1 }'); do
        if adb -s "$d" shell dumpsys usb | awk '/status=\{/,/role_combinations/' | grep -q "power_role=source"; then
            adb -s "$d" shell dumpsys usb set-port-roles port0 sink device >/dev/null
            echo "$(date +%T) phone $d: power role source -> sink"
        fi
    done
done
