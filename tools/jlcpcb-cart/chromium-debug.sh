#!/usr/bin/env bash
# Start a Chromium with the DevTools port open, on a dedicated profile (Chrome >= 136
# refuses remote debugging on the default profile).  Log in to jlcpcb.com in it once;
# the profile keeps the session.  Works from an ssh shell on a box with a GNOME desktop.
set -euo pipefail
PROFILE="${HOME}/.config/jlcpcb-chromium"
PORT="${1:-9222}"
URL="${2:-https://jlcpcb.com/user-center/smtPrivateLibrary/partsCart/}"   # pass https://www.lcsc.com/ for the LCSC cart
BIN=$(command -v chromium || command -v chromium-browser || command -v google-chrome)
XA=$(ls /run/user/"$(id -u)"/.mutter-Xwaylandauth.* 2>/dev/null | head -1 || true)
if curl -s "http://127.0.0.1:${PORT}/json/version" >/dev/null; then echo "already listening on :${PORT}"; exit 0; fi
env DISPLAY="${DISPLAY:-:0}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}" XDG_RUNTIME_DIR="/run/user/$(id -u)" \
    DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus" ${XA:+XAUTHORITY="$XA"} \
    setsid nohup "$BIN" --remote-debugging-port="$PORT" --user-data-dir="$PROFILE" --no-first-run \
    --new-window "$URL" >/dev/null 2>&1 &
for _ in $(seq 1 30); do curl -s "http://127.0.0.1:${PORT}/json/version" >/dev/null && { echo "chromium listening on :${PORT}, log in in the window ($URL)"; exit 0; }; sleep 1; done
echo "chromium did not open the debug port" >&2; exit 1
