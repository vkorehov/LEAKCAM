Android bench phone (the BLE driver of tests/test040, adb over USB)

The phone must charge from the computer. On this setup USB-C role negotiation made the phone the power source: it
charged the laptop and ran down to 3 % (2026-10-28), and again after a reconnect. The fix is a role swap:
    adb shell dumpsys usb set-port-roles port0 sink device
It holds only until the next USB reconnect, so phone_sink.sh watches adb and swaps every phone that comes up as source.
test040's run.py also swaps at its start.

Run it for a session:    tools/android-bench/phone_sink.sh &
Install it for good (once per machine, by the user):
    ln -sf ~/k230d-hw/LEAKCAM/tools/android-bench/phone-sink.service ~/.config/systemd/user/
    systemctl --user daemon-reload && systemctl --user enable --now phone-sink.service
Check: journalctl --user -u phone-sink -n 20; by hand: adb shell dumpsys usb | grep power_role
