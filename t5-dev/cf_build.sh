#!/usr/bin/env bash
# Build the cyber_fortune firmware for the Tuya T5AI-Board.
#
# Usage:
#   bash cf_build.sh          # incremental build (fast)
#   bash cf_build.sh clean    # full clean first (needed after a BOARD/config change)
#
# China-network notes:
#   - TZ=Asia/Shanghai makes TuyaOpen pick the Gitee mirrors for git submodules.
#   - Unsetting *_PROXY + no_proxy='*' bypasses a dead/stale Clash proxy.
set -e

APP_DIR=/Users/oliver/data/projects/moneyGod/t5-dev/TuyaOpen/apps/cyber_fortune
cd "$APP_DIR"

# activate the TuyaOpen environment (adds tos.py to PATH)
. ../../export.sh >/dev/null 2>&1

# kill any stale proxy env pointing at the dead Clash endpoint, force Gitee mirrors
unset HTTPS_PROXY HTTP_PROXY ALL_PROXY https_proxy http_proxy all_proxy
export TZ=Asia/Shanghai
export no_proxy='*'
export NO_PROXY='*'

if [ "$1" = "clean" ]; then
    echo "=== clean (full, board/config changed) ==="
    tos.py clean
fi

# config choice MUST run after clean (clean wipes the selected config).
echo "=== config choice ==="
tos.py config choice -c TUYA_T5AI_BOARD_LCD_3.5

echo "=== BUILD START ==="
tos.py build
echo "=== BUILD DONE (exit $?) ==="
