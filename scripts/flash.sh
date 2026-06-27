#!/usr/bin/env bash
# Flasht das gemergte Image aufs Device (Port auto). Args werden durchgereicht,
# z.B.:  ./scripts/flash.sh --port /dev/cu.usbmodem1101 --erase
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/flasher/flash.py "$@"
