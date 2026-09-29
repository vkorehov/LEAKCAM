#!/usr/bin/env bash
# Chromium with DevTools port open on a dedicated AliExpress profile.
# Same pattern as tools/jlcpcb-cart/chromium-debug.sh, separate profile + port.
set -euo pipefail
PROFILE="${HOME}/.config/aliexpress-chromium"
PORT="${1:-9223}"
URL="${2:-https://www.aliexpress.com/}"
BIN=$(command -v chromium || command -v chromium-browser || command -v google-chrome)
XA=$(ls /run/user/"$(id -u)"/.mutter-Xwaylandauth.* 2>/dev/null | head -1 || true)
if curl -s "http://127.0.0.1:${PORT}/json/version" >/dev/null; then echo "already listening on :${PORT}"; exit 0; fi
env DISPLAY="${DISPLAY:-:0}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}" XDG_RUNTIME_DIR="/run/user/$(id -u)" \
    DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus" ${XA:+XAUTHORITY="$XA"} \
    setsid nohup "$BIN" --remote-debugging-port="$PORT" --user-data-dir="$PROFILE" --no-first-run \
    --new-window "$URL" >/dev/null 2>&1 &
for _ in $(seq 1 30); do curl -s "http://127.0.0.1:${PORT}/json/version" >/dev/null && { echo "chromium listening on :${PORT} ($URL)"; exit 0; }; sleep 1; done
echo "chromium did not open the debug port" >&2; exit 1
