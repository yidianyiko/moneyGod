#!/bin/bash
# Serial monitor for the CF Stick (115200 baud, VID 0x303A auto-select).
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/cf_stick"
export PLATFORMIO_CORE_DIR="$DIR/.pio_core"
PIO="$DIR/.venv/bin/pio"
PORT=$("$PIO" device list --json-output | python3 -c "
import json,sys
for d in json.load(sys.stdin):
    if '303A' in d.get('hwid','').upper():
        print(d['port']); break
")
"$PIO" device monitor -b 115200 ${PORT:+-p "$PORT"}
