#!/usr/bin/env bash
# Flash the cyber_fortune firmware to the connected Tuya T5AI-Board.
set -e

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "$SCRIPT_DIR/TuyaOpen/apps/cyber_fortune"

. ../../export.sh >/dev/null 2>&1

unset HTTPS_PROXY HTTP_PROXY ALL_PROXY https_proxy http_proxy all_proxy
export TZ=Asia/Shanghai
export no_proxy='*'
export NO_PROXY='*'

PORT="${1:-/dev/cu.usbmodem5AAE1672501}"
BAUD="${2:-921600}"

echo "=== FLASH START (port=$PORT baud=$BAUD) ==="
tos.py flash -p "$PORT" -b "$BAUD"
echo "=== FLASH DONE (exit $?) ==="
