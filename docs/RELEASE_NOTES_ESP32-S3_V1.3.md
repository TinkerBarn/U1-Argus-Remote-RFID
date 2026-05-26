# ESP32-S3 Dual-Reader V1.3

ESP32-S3 Dual-Reader V1.3 introduces signed firmware updates and reduces
background load when a filament spool remains parked in front of a sensor.

## Changes

- Introduces a permanent 16 MB partition layout with two `6.25 MiB` OTA
  application slots and reserved LittleFS capacity for future protocol work.
- Adds signed OTA upload through the Setup page. Uploaded firmware is verified
  before it can boot.
- Adds upload progress, verification/restart status, and automatic setup-page
  reconnection after a successful OTA reboot.
- Adds automatic update discovery through the published
  `updates/esp32-s3.json` catalog, with a download link for the newest signed
  OTA file when a newer version is available.
- Throttles an unchanged tag that remains continuously at one PN532: after
  3 seconds, that reader checks once every 2 seconds instead of repeatedly
  processing identical payloads.
- Continues one printer comparison after each throttled check so a missed SET
  or later printer mismatch can still be corrected.

## Installation Files

- Use the `.ino.merged.bin` image through the Web Installer for a new
  installation or for the one-time migration from `V1.0`-`V1.2`.
- Use the `.ota.signed.bin` image from the Setup web interface for updates
  after `V1.3` has installed the permanent dual-slot layout.

## Retained Baseline

- Fast OpenSpool/NTAG and QIDI/MIFARE Classic reading over raw PN532 HSU.
- FreeRTOS NFC polling task on Core 0 with the web/printer workflow on Core 1.
- Preferred BSSID support, BSSID/RSSI visibility, and persisted additional
  reader Tool Head assignments.
- Filament-loaded safety lock that blocks metadata replacement after filament
  reaches its assigned Tool Head.

## Arduino IDE Note

Manual builds require the complete `source/ESP32-S3/V1.3/` folder with its
`partitions.csv`, `build_opt.h`, and `ota_public_key.h` companion files. Use
`ESP32S3 Dev Module`, `16MB (128Mb)`, and the included custom partition table.
