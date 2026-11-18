#!/usr/bin/env bash
set -Eeuo pipefail
[[ $EUID -eq 0 ]] || { echo 'Run with sudo.'; exit 1; }
cd /opt/group7-mqtt
docker compose down
systemctl enable --now mosquitto
systemctl status mosquitto --no-pager
echo 'Original systemd broker restored. Its data is the pre-migration copy.'
