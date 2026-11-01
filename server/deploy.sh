#!/bin/bash
# Build for the Pi (vhome, aarch64) and (re)start it there as the systemd unit leakcam-server.
#   server/deploy.sh [poll seconds]     the interval goes into ~/leakcam-server/env (tests use a short one), with the
#                                       BLE pairing PIN key from ../.secrets/pairing_secret (the firmware's; mode 600)
set -euo pipefail
HOST=pi@192.168.1.159
cd "$(dirname "$0")"
KEY=$(tr -d ' \n' < ../.secrets/pairing_secret)
[ ${#KEY} -eq 64 ] || { echo "no 64-hex-digit key in .secrets/pairing_secret"; exit 1; }
go vet .
GOOS=linux GOARCH=arm64 go build -ldflags "-s -w" -o /tmp/leakcam-server.$$ .
ssh "$HOST" 'mkdir -p ~/leakcam-server'
scp -q /tmp/leakcam-server.$$ "$HOST:leakcam-server/leakcam-server.new"
rm -f /tmp/leakcam-server.$$
scp -q leakcam-server.service "$HOST:leakcam-server/"
# the key over stdin, never on a command line (process lists)
printf 'LEAKCAM_POLL_S=%s\nLEAKCAM_PIN_KEY=%s\n' "${1:-60}" "$KEY" | ssh "$HOST" 'umask 077 && cat > ~/leakcam-server/env && chmod 600 ~/leakcam-server/env'
ssh "$HOST" "mv ~/leakcam-server/leakcam-server.new ~/leakcam-server/leakcam-server &&
  sudo cp ~/leakcam-server/leakcam-server.service /etc/systemd/system/ && sudo systemctl daemon-reload &&
  sudo systemctl enable --now leakcam-server && sudo systemctl restart leakcam-server && sleep 1 &&
  systemctl is-active leakcam-server"
