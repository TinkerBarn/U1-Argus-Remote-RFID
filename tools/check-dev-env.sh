#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARDUINO_CLI="$ROOT_DIR/tools/arduino-cli"
ESP32_PACKAGE_URL="https://espressif.github.io/arduino-esp32/package_esp32_index.json"

echo "Project: $ROOT_DIR"
echo

echo "Arduino CLI:"
"$ARDUINO_CLI" version
echo

echo "Arduino IDE:"
if [[ -d "/Applications/Arduino IDE.app" ]]; then
  echo "OK: /Applications/Arduino IDE.app"
else
  echo "MISSING: /Applications/Arduino IDE.app"
fi
echo

echo "Board packages:"
if "$ARDUINO_CLI" core list | grep -q '^esp32:esp32[[:space:]]'; then
  "$ARDUINO_CLI" core list | grep -E '^(ID|esp32:esp32)'
else
  echo "MISSING: esp32:esp32"
  echo "Install: $ARDUINO_CLI core update-index --additional-urls \"$ESP32_PACKAGE_URL\""
  echo "Then:    $ARDUINO_CLI core install esp32:esp32 --additional-urls \"$ESP32_PACKAGE_URL\""
fi
echo

echo "Arduino ctags / Rosetta:"
CTAGS_BIN="$HOME/Library/Arduino15/packages/builtin/tools/ctags/5.8-arduino11/ctags"
if [[ -x "$CTAGS_BIN" ]]; then
  file "$CTAGS_BIN"
  if [[ "$(uname -m)" == "arm64" ]] && file "$CTAGS_BIN" | grep -q 'x86_64'; then
    if "$CTAGS_BIN" --version >/dev/null 2>&1; then
      echo "OK: Rosetta can execute Arduino's Intel ctags tool."
    else
      echo "MISSING: Rosetta 2 is required because Arduino ctags is x86_64."
      echo "Install manually: softwareupdate --install-rosetta --agree-to-license"
    fi
  fi
else
  echo "MISSING: Arduino ctags tool"
fi
echo

echo "Libraries:"
missing=0
for lib in "Adafruit PN532" "ArduinoJson"; do
  if "$ARDUINO_CLI" lib list | grep -Fq "$lib"; then
    "$ARDUINO_CLI" lib list | grep -F "$lib"
  else
    echo "MISSING: $lib"
    missing=1
  fi
done
if [[ "$missing" -ne 0 ]]; then
  echo "Install: $ARDUINO_CLI lib install \"Adafruit PN532\" ArduinoJson"
fi
echo

echo "Serial ports:"
if compgen -G "/dev/cu.*" >/dev/null; then
  ls -l /dev/cu.*
else
  echo "No /dev/cu.* serial ports found."
fi
echo

echo "macOS permissions:"
echo "- For flashing from Arduino IDE or browser, allow serial/USB access when macOS asks."
echo "- If the board does not appear, install the USB serial driver required by your ESP32-C3 board chipset."
echo "- Current user: $(id -un)"
