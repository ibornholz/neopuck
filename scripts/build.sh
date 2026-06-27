#!/usr/bin/env bash
# Baut die Firmware und merged alles zu einem einzigen Image (0x0).
set -euo pipefail
cd "$(dirname "$0")/.."

if [ -z "${IDF_PATH:-}" ]; then
  echo "ESP-IDF nicht aktiv. Zuerst:  . \$IDF_PATH/export.sh" >&2
  exit 1
fi

idf.py set-target esp32s3
idf.py build

mkdir -p dist
# erzeugt ein flashbares Gesamt-Image (bootloader + part-table + app)
idf.py merge-bin -o dist/neopuck-merged.bin

echo
echo "OK -> dist/neopuck-merged.bin"
