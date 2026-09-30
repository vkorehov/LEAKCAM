# LEAKCAM Setup (Android)

Sets the Wi-Fi network a LEAKCAM uses. One screen: scan, pick the board, type the SSID and
password, save. Android 8.0 (API 26) and later, no libraries beyond the Android framework.

## Using it

1. Plug the LEAKCAM into USB power. Only then does it advertise its setup service and accept
   pairing: being able to plug it in is the proof of physical access.
2. **Scan for LEAKCAM** lists every board in range as `LEAKCAM-xxyy` with its signal strength.
3. Enter the Wi-Fi name and password (empty password for an open network), then
   **Save to LEAKCAM**. Accept the pairing request the phone shows the first time.
4. "Saved" means the board stored both in its flash. It does not test the network: it joins at
   its next wake, and a wrong password shows as the board never reporting.

If saving fails with a pairing error on a board that was set up before, remove it from the
phone's Bluetooth devices (its bond was erased) and save again.

## What it sends

The BL616's provisioning service (`firmware/bl616/ble_pairing.h`), over an encrypted link:

| Characteristic | UUID | Value |
|---|---|---|
| service | `4c43a000-4c45-4b43-414d-000000000001` | |
| SSID | `...0002` | UTF-8, 1-32 bytes |
| passphrase | `...0003` | 8-63 bytes, or empty |
| commit | `...0004` | `0x01`: store both |

The app asks for an MTU of 247 first, so a 63-byte passphrase goes in one write (the board
rejects offset writes), then pairs, then writes the three in order.

## Building

Open this folder in Android Studio, or:

```bash
./gradlew assembleDebug                 # app/build/outputs/apk/debug/app-debug.apk
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

The APK is signed with the debug key: fine for installing on your own phones, not for a store.

**On an aarch64 Linux host** Google publishes `aapt2` for x86-64 only. It runs under
qemu-user 11.1 (the one in `firmware/k230_nn/qemu11`) with an x86-64 libc taken from the
`python:3.11-slim` image:

```bash
mkdir -p ~/.local/opt/aapt2-x86 && cd ~/.local/opt/aapt2-x86
V=8.7.3-12006047                        # the aapt2 of AGP 8.7.3
curl -sSLO https://dl.google.com/android/maven2/com/android/tools/build/aapt2/$V/aapt2-$V-linux.jar
unzip -oq aapt2-$V-linux.jar aapt2 && mv aapt2 aapt2.bin
mkdir sysroot && cid=$(docker create --platform linux/amd64 python:3.11-slim)
docker export $cid | tar -x -C sysroot lib lib64 usr/lib/x86_64-linux-gnu usr/lib64; docker rm $cid
printf '#!/bin/sh\nexec %s -L %s %s "$@"\n' ~/k230d-hw/LEAKCAM/firmware/k230_nn/qemu11/usr/bin/qemu-x86_64 \
  ~/.local/opt/aapt2-x86/sysroot ~/.local/opt/aapt2-x86/aapt2.bin > aapt2 && chmod +x aapt2

cd ~/k230d-hw/LEAKCAM/android
./gradlew assembleDebug -Pandroid.aapt2FromMavenOverride=$HOME/.local/opt/aapt2-x86/aapt2
```

The SDK needs `platforms;android-35` and `build-tools;35.0.0` (`sdkmanager`), and
`local.properties` with `sdk.dir=<sdk path>`.
