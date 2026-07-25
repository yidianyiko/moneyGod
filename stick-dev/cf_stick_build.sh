#!/bin/bash
# Build the CF Stick firmware (StickS3).
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/cf_stick"
unset http_proxy https_proxy all_proxy
export PLATFORMIO_CORE_DIR="$DIR/.pio_core"
"$DIR/.venv/bin/pio" run -e m5stack-sticks3 "$@"
