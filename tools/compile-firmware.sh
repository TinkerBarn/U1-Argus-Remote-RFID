#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARDUINO_CLI="$ROOT_DIR/tools/arduino-cli"
FQBN="${FQBN:-esp32:esp32:esp32c3}"
MAX_APP_SIZE="${MAX_APP_SIZE:-}"
BUILD_ROOT="$ROOT_DIR/.build"
SKETCH_SRC="${1:-$ROOT_DIR/source/ESP32-C3/V2.2/U1_Argus_Remote_RFID_ESP32-C3_V2_2.ino}"
CTAGS_BIN="$HOME/Library/Arduino15/packages/builtin/tools/ctags/5.8-arduino11/ctags"

if [[ ! -f "$SKETCH_SRC" ]]; then
  echo "Sketch not found: $SKETCH_SRC" >&2
  exit 2
fi

if [[ "$(uname -m)" == "arm64" ]] && [[ -x "$CTAGS_BIN" ]] && file "$CTAGS_BIN" | grep -q 'x86_64'; then
  if ! "$CTAGS_BIN" --version >/dev/null 2>&1; then
    cat >&2 <<'EOF'
Arduino's bundled ctags is an Intel x86_64 binary, but Rosetta 2 is not available.

Install Rosetta on this Apple Silicon Mac, then run this command again:
  softwareupdate --install-rosetta --agree-to-license
EOF
    exit 69
  fi
fi

SKETCH_BASE="$(basename "$SKETCH_SRC" .ino)"
SKETCH_DIR="$BUILD_ROOT/sketches/$SKETCH_BASE"
OUT_DIR="$BUILD_ROOT/firmware/$SKETCH_BASE"

rm -rf "$SKETCH_DIR" "$OUT_DIR"
mkdir -p "$SKETCH_DIR" "$OUT_DIR"
cp "$SKETCH_SRC" "$SKETCH_DIR/$SKETCH_BASE.ino"
if [[ -f "$(dirname "$SKETCH_SRC")/partitions.csv" ]]; then
  cp "$(dirname "$SKETCH_SRC")/partitions.csv" "$SKETCH_DIR/partitions.csv"
fi
for COMPANION_HEADER in "$(dirname "$SKETCH_SRC")"/*.h; do
  if [[ -f "$COMPANION_HEADER" ]]; then
    cp "$COMPANION_HEADER" "$SKETCH_DIR/$(basename "$COMPANION_HEADER")"
  fi
done

COMPILE_ARGS=(
  compile
  --fqbn "$FQBN"
  --warnings default
  --output-dir "$OUT_DIR"
)
if [[ -n "$MAX_APP_SIZE" ]]; then
  COMPILE_ARGS+=(--build-property "upload.maximum_size=$MAX_APP_SIZE")
fi
COMPILE_ARGS+=("$SKETCH_DIR")

"$ARDUINO_CLI" "${COMPILE_ARGS[@]}"

echo
echo "Build output: $OUT_DIR"
