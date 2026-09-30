# LEAKCAM Setup (Android)

The user experience, screen by screen and message by message: [APP.txt](APP.txt).

Sets the Wi-Fi network a LEAKCAM uses. One screen: scan, pick the board, type the SSID and
password, save. Android 8.0 (API 26) and later, no libraries beyond the Android framework.

## Using it

The screen lists four steps and highlights the current one; the box under them says what to do
now and, when something fails, why and what to try.

1. **Plug the LEAKCAM into USB power.** Only then does it advertise its setup service and accept
   pairing: being able to plug it in is the proof of physical access.
2. **Find it.** *Find LEAKCAM* lists every board in range as `LEAKCAM-xxyy` with its signal
   strength; the nearest usually has the strongest.
3. **Enter your Wi-Fi network**: a 2.4 GHz one (the board has no 5 GHz radio); empty password
   for an open network.
4. **Pair, check and save.** The first time, the phone asks to pair with `LEAKCAM-xxyy`: tap Pair
   (or Allow). There is no PIN, the board has no screen. If no dialog appears, the request is in
   the notification shade. The app then sends the settings and the LEAKCAM checks them itself:
   it joins the network, gets an address and reaches `connectivitycheck.gstatic.com`, which takes
   up to a minute. The app shows each step.

"Done" means the check passed and the board stored the network. When it fails, nothing is
stored and the app says why: network not seen (name, 5 GHz, out of reach), wrong password,
router refused, no address, no DNS, no internet, or a network with a login page. Where the
failure may only be about where the board is now, **Save without checking** stores the network
anyway.

If a board that was paired before was reset, the app says so and asks to remove it in the
phone's Bluetooth settings (Forget) before saving again.

## What it sends

The BL616's provisioning service (`firmware/bl616/ble_pairing.h`), over an encrypted link:

| Characteristic | UUID | Value |
|---|---|---|
| service | `4c43a000-4c45-4b43-414d-000000000001` | |
| SSID | `...0002` | UTF-8, 1-32 bytes |
| passphrase | `...0003` | 8-63 bytes, or empty |
| commit | `...0004` | `0x01`: check, then store; `0x02`: store without checking |
| status | `...0005` | read/notify: state, detail (LE16), `firmware/bl616/wifi_check_calc.h` |

The app asks for an MTU of 247 first, so a 63-byte passphrase goes in one write (the board
rejects offset writes), then pairs, subscribes to status, then writes the three in order and
follows status to the end.

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
