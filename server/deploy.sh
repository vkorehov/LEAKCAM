#!/bin/bash
# Build for the Pi (vhome, aarch64) and (re)start it there as the systemd unit leakcam-server.
#   server/deploy.sh [poll seconds]     the interval goes into ~/leakcam-server/env (tests use a short one)
set -euo pipefail
HOST=pi@192.168.1.159
cd "$(dirname "$0")"
go vet .
GOOS=linux GOARCH=arm64 go build -ldflags "-s -w" -o /tmp/leakcam-server.$$ .
ssh "$HOST" 'mkdir -p ~/leakcam-server'
scp -q /tmp/leakcam-server.$$ "$HOST:leakcam-server/leakcam-server.new"
rm -f /tmp/leakcam-server.$$
scp -q leakcam-server.service "$HOST:leakcam-server/"
ssh "$HOST" "mv ~/leakcam-server/leakcam-server.new ~/leakcam-server/leakcam-server &&
  echo 'LEAKCAM_POLL_S=${1:-60}' > ~/leakcam-server/env &&
  sudo cp ~/leakcam-server/leakcam-server.service /etc/systemd/system/ && sudo systemctl daemon-reload &&
  sudo systemctl enable --now leakcam-server && sudo systemctl restart leakcam-server && sleep 1 &&
  systemctl is-active leakcam-server"
