#!/usr/bin/env bash
# Baut die Firmware und merged alles zu einem einzigen Image (0x0).
set -euo pipefail
cd "$(dirname "$0")/.."

if [ -z "${IDF_PATH:-}" ]; then
  echo "ESP-IDF nicht aktiv. Zuerst:  . \$IDF_PATH/export.sh" >&2
  exit 1
fi

# Build-Verzeichnis AUSSERHALB des Projekts (Desktop ist oft iCloud-synchronisiert;
# das dupliziert build/-Dateien während des Compiles -> "Directory not empty"-Abbruch).
BUILD_DIR="${NEOPUCK_BUILD_DIR:-/tmp/neopuck-build}"

idf.py -B "$BUILD_DIR" set-target esp32s3
idf.py -B "$BUILD_DIR" build

mkdir -p dist
# erzeugt ein flashbares Gesamt-Image (bootloader + part-table + app)
idf.py -B "$BUILD_DIR" merge-bin -o "$PWD/dist/neopuck-merged.bin"

echo
echo "OK -> dist/neopuck-merged.bin"
