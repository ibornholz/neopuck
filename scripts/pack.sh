#!/usr/bin/env bash
# Ein Befehl: bauen + mergen + draufflashen. "Alles aufs Device packen."
set -euo pipefail
cd "$(dirname "$0")/.."
./scripts/build.sh
./scripts/flash.sh "$@"
