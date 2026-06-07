# ESP32-S3 Dual-Reader V1.6

ESP32-S3 Dual-Reader V1.6 keeps the V1.5 RFID, OTA, WebSocket, and card identity
behavior while adding a printer-state self-heal path for channels that lose
their filament metadata after a successful external RFID update.

## Highlights

- Detects when the printer reports a previously updated channel as cleared
  (`NONE` / no card UID / zero temperatures) after the reader had successfully
  sent a valid tag payload.
- Re-sends the last successfully sent tag payload for that reader as a
  self-heal repair, with a per-reader cooldown to avoid rapid repeated SETs.
- Allows the self-heal repair both when `filament_detected=false` and when
  `filament_detected=true`.
- Keeps normal safety behavior for new tags:
  - when `filament_detected=false`, a newly detected tag can still replace the
    previous payload normally;
  - when `filament_detected=true`, normal new-tag changes remain blocked.
- Keeps duplicate-send protection for an unchanged tag that remains parked in
  front of the PN532 reader.
- Performs slow periodic filament-info synchronization even while the Moonraker
  WebSocket is connected, preventing stale dashboard states such as very old
  `updated` ages.

## Firmware Files

- First-install / web-installer binary:
  `firmware/ESP32-S3/V1.6/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_6.ino.merged.bin`
- Signed OTA update:
  `firmware/ESP32-S3/V1.6/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_6.ota.signed.bin`
- Arduino source:
  `source/ESP32-S3/V1.6/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_6.ino`

## Notes

The ESP32-S3 V1.6 build still requires the custom 16 MB dual-slot partition
layout from `source/ESP32-S3/V1.6/partitions.csv`. When building from Arduino
IDE, open the complete `source/ESP32-S3/V1.6/` folder so `partitions.csv`,
`build_opt.h`, and `ota_public_key.h` are included.
