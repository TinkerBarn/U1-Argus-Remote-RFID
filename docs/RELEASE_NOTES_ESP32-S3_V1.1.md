# ESP32-S3 Dual-Reader V1.1

ESP32-S3 Dual-Reader V1.1 is a focused update to the first S3 release. It
keeps the fast, hardware-confirmed OpenSpool and QIDI RFID path while improving
Wi-Fi visibility and multi-reader setup persistence.

## Changes

- The dashboard Network tile now shows the BSSID of the currently connected
  2.4 GHz access point.
- The dashboard and normal serial log show visible BSSIDs for the configured
  SSID with RSSI and Wi-Fi channel information.
- BSSID scan results are cached and refreshed outside the NFC task; scans are
  deferred while a tag is active.
- Additional dual-reader left/right Tool Head assignments now persist after a
  save and reboot. Earlier S3 versions attempted to use Preferences keys that
  exceeded the ESP32 NVS key length limit.

## Retained RFID Baseline

- Two independent PN532 readers in HSU/UART mode.
- OpenSpool/NTAG and QIDI/MIFARE Classic tag support.
- NFC polling in a FreeRTOS task on Core 0 while the Arduino main loop handles
  Wi-Fi, dashboard, and printer requests on Core 1.

## Arduino IDE Upload Note

For the tested ESP32-S3 dual-reader board, use `ESP32S3 Dev Module` with:

- `Flash Size`: `4MB (32Mb)`
- `Partition Scheme`: `Huge APP (3MB No OTA/1MB SPIFFS)`
- `PSRAM`: `OPI PSRAM`
- `Arduino Runs On`: `Core 1`
- `Events Run On`: `Core 1`

Although this board is sold as N16R8, the tested screw-terminal expansion-board
unit reported a 4 MB accessible flash window at boot. Do not use `16M Flash
(3MB APP/9.9MB FATFS)` for this tested setup; it caused a boot-loop partition
validation failure.

## Upgrade Notes

- Web-installer updates retain saved Wi-Fi, printer, BSSID, and QIDI mapping
  settings because the installer no longer prompts for an erase operation.
- After upgrading from V1.0, enter and save additional-reader Tool Head
  assignments once so they are stored with the corrected keys.
