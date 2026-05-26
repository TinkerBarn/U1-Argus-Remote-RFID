# ESP32-S3 Dual-Reader V1.2

ESP32-S3 Dual-Reader V1.2 adds a filament-loaded safety lock to the fast,
hardware-confirmed dual-reader baseline.

## Changes

- Before sending tag data to the printer, the firmware now performs a fresh
  filament motion-sensor check for the assigned Tool Head.
- A tag-triggered `filament_detect/set` is sent only when the assigned Tool
  Head reports `filament_detected=false`.
- Once filament is detected at a Tool Head, RFID polling is paused for that
  spool position so an accidental read cannot overwrite active filament
  metadata.
- Reader pausing is independent for the two local readers: a loaded spool on
  one side does not prevent the other side from accepting a new spool.
- While filament remains loaded, printer motion polling for that channel is
  reduced to approximately one query every five seconds.
- If SET retries are enabled in a later build, they are protected by the same
  filament-state gate.

## Retained Baseline

- Based on the hardware-tested V0.30 development firmware.
- Retains the fast QIDI/MIFARE Classic and OpenSpool/NTAG PN532 HSU read path.
- Retains BSSID preference support, BSSID/RSSI visibility, saved additional
  reader Tool Head assignments, and the Core 0/Core 1 FreeRTOS split.

## Upload Note

For the tested ESP32-S3 dual-reader board, use `ESP32S3 Dev Module` with
`4MB (32Mb)`, `Huge APP (3MB No OTA/1MB SPIFFS)`, and `OPI PSRAM`.
