#!/bin/bash
# Flash the CF Stick firmware. Auto-selects the port by USB VID 0x303A
# (Espressif native USB) -- do NOT rely on the usbmodem prefix, the T5's
# CH342 also enumerates as usbmodem on macOS.
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/cf_stick"
unset http_proxy https_proxy all_proxy
export PLATFORMIO_CORE_DIR="$DIR/.pio_core"
PIO="$DIR/.venv/bin/pio"
PORT=$("$PIO" device list --json-output | python3 -c "
import json,sys
for d in json.load(sys.stdin):
    hwid = d.get('hwid','')
    if '303A' in hwid.upper():
        print(d['port']); break
")
if [ -z "$PORT" ]; then echo "StickS3 not found (VID 303A)"; exit 1; fi
echo "Flashing to $PORT"
"$PIO" run -e m5stack-sticks3 -t upload --upload-port "$PORT"
