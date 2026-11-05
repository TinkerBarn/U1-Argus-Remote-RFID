# ESP32-S3 Dual-Reader V1.7

ESP32-S3 Dual-Reader V1.7 improves moving-tag detection and makes the complete
RFID-to-printer path visible from the dashboard. It retains the V1.6 signed
OTA, filament-loaded safety lock, printer self-heal, Wi-Fi/BSSID, card identity,
and PN532 recovery behavior.

## Highlights

- Keeps both unlocked readers at a 30 ms minimum polling interval after feeder
  activity; V1.6 changed to a 900 ms idle interval after the first detected
  feeder event.
- Shortens exclusive feeder priority from 12 seconds to 4 seconds so the other
  unloaded reader resumes scanning sooner.
- Sends a completely read and mapped QIDI or OpenSpool payload immediately,
  including `CARD_UID`, after the filament safety check. Partial reads never
  trigger a UID-only update.
- Keeps the safety lock: normal new-tag SET requests remain blocked when the
  assigned Tool Head reports `filament_detected=true` or its current sensor
  state cannot be confirmed.
- Adds timestamped flow diagnostics for UID detection, OpenSpool content-read
  success/failure, queue handoff latency, sensor transitions, blocked SETs,
  and printer HTTP results.
- Mirrors DebugSerial output to a live dashboard panel when the saved
  **Debug serial log** setting is enabled. The panel supports expand, pause,
  auto-scroll, clear, and text download.
- Uses a bounded 16 KiB in-memory device log and a bounded 64 KiB browser view;
  logs are not written to flash.
- Adds Setup save controls above and below the settings and a fixed bottom save
  bar, so the DebugSerial setting remains reachable on desktop and mobile.
- Preserves the PN532 RF-field switching, RF power behavior, 60 ms tag-detect
  timeouts, reader wiring, partition layout, and per-reader loaded-state lock.

## Firmware Files

- First-install / web-installer binary:
  `firmware/ESP32-S3/V1.7/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_7.ino.merged.bin`
- Signed OTA update:
  `firmware/ESP32-S3/V1.7/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_7.ota.signed.bin`
- Arduino source:
  `source/ESP32-S3/V1.7/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_7.ino`

SHA-256:

- merged binary: `4a45a939844f139827ec82a7b63f3e5db64da01c828b1408b940d347a5c36228`
- signed OTA: `8e414c86a8b3dd19d1ba4be778ef809edff415af7284ca075609dc293b223f18`

## Installation

Existing V1.3 and later installations can install the signed OTA file from
Setup. V1.0-V1.2 require one browser/USB installation of V1.3 or later to gain
the permanent dual-slot OTA layout. A first installation can use the browser
web installer and the merged binary.

The ESP32-S3 V1.7 build requires an N16R8 board with 16 MB flash and the custom
dual-slot layout from `source/ESP32-S3/V1.7/partitions.csv`. Open the complete
V1.7 source folder in Arduino IDE so `partitions.csv`, `build_opt.h`,
`ota_public_key.h`, `debug_log_buffer.h`, and `debug_log_panel.h` are included.
