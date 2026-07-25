#!/usr/bin/env bash
# Capture the serial boot/run log from the Tuya T5AI-Board.
#
# Usage:
#   bash cf_monitor.sh                 # default log port 503 @ 460800
#   bash cf_monitor.sh <port> <baud>
#
# Notes (T5AI-Board, learned the hard way):
#   - The LOG comes out of the *503* CDC port, NOT the 501 flashing port.
#   - The T5AI default log baudrate is 460800 (115200 => garbage).
#   - CH343 RESET is NOT wired to RTS/DTR, so you must press the physical
#     RST button to see the boot banner; the app only logs once at startup.
set -e

cd /Users/oliver/data/projects/moneyGod/t5-dev/TuyaOpen/apps/cyber_fortune
. ../../export.sh >/dev/null 2>&1

unset HTTPS_PROXY HTTP_PROXY ALL_PROXY https_proxy http_proxy all_proxy
export no_proxy='*'
export NO_PROXY='*'

PORT="${1:-/dev/cu.usbmodem5AAE1672503}"
BAUD="${2:-460800}"
LOG="${3:-/tmp/cf_mon.log}"

echo "=== MONITOR (port=$PORT baud=$BAUD log=$LOG) — press the board RST button; Ctrl+] to quit ==="
tos.py monitor -p "$PORT" -b "$BAUD" -l "$LOG"
