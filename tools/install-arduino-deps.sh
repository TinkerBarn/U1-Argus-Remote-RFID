#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARDUINO_CLI="$ROOT_DIR/tools/arduino-cli"
ESP32_PACKAGE_URL="https://espressif.github.io/arduino-esp32/package_esp32_index.json"

"$ARDUINO_CLI" core update-index --additional-urls "$ESP32_PACKAGE_URL"
"$ARDUINO_CLI" core install esp32:esp32 --additional-urls "$ESP32_PACKAGE_URL"
"$ARDUINO_CLI" lib install ArduinoJson "Adafruit PN532"

echo
echo "Arduino dependencies installed."
echo "On Apple Silicon, install Rosetta if compile fails at Arduino ctags:"
echo "softwareupdate --install-rosetta --agree-to-license"
