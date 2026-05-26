#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARDUINO_CLI="$ROOT_DIR/tools/arduino-cli"
FQBN="${FQBN:-esp32:esp32:esp32c3}"
PORT="${1:-}"
SKETCH_SRC="${2:-$ROOT_DIR/source/ESP32-C3/V2.2/U1_Argus_Remote_RFID_ESP32-C3_V2_2.ino}"

if [[ -z "$PORT" ]]; then
  echo "Usage: $0 /dev/cu.<device> [source/ESP32-C3/Vx.y/file.ino]" >&2
  echo
  echo "Available ports:"
  "$ARDUINO_CLI" board list
  exit 2
fi

"$ROOT_DIR/tools/compile-firmware.sh" "$SKETCH_SRC"

SKETCH_BASE="$(basename "$SKETCH_SRC" .ino)"
SKETCH_DIR="$ROOT_DIR/.build/sketches/$SKETCH_BASE"

"$ARDUINO_CLI" upload \
  --fqbn "$FQBN" \
  --port "$PORT" \
  "$SKETCH_DIR"
