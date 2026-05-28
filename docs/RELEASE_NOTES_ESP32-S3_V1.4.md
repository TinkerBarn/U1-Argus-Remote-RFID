# ESP32-S3 Dual-Reader V1.4

ESP32-S3 Dual-Reader V1.4 is a focused setup-interface release. It keeps the
V1.3 RFID, printer-safety, OTA, BSSID, and dual-reader behavior intact while
making setup easier to scan.

## Highlights

- Refines the setup page with clearly separated panels for network, reader,
  printer, mapping, diagnostics, QIDI, and firmware-update settings.
- Uses smaller, quieter buttons and more compact form controls.
- Keeps the captive portal focused on first setup by hiding advanced sections
  such as QIDI config upload, signed firmware update, diagnostics, and
  additional-reader configuration until Setup is opened from the normal
  dashboard.
- Delays the S3 update-catalog check slightly so the setup page can render
  first.

## Unchanged From V1.3

- Fast OpenSpool/NTAG and QIDI/MIFARE Classic reading over raw PN532 HSU.
- Dedicated ESP32-S3 NFC task with Wi-Fi, dashboard, and printer communication
  on the Arduino main loop.
- Filament-loaded safety lock before sending tag updates.
- Per-spool pause while filament remains detected.
- Signed OTA update workflow with progress and automatic reconnect.
- Preferred BSSID support and visible same-SSID BSSID/RSSI reporting.
- Permanent 16 MB dual-slot OTA partition layout.

## Files

- First-install / web-installer image:
  `firmware/ESP32-S3/V1.4/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_4.ino.merged.bin`
- Signed OTA update image:
  `firmware/ESP32-S3/V1.4/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_4.ota.signed.bin`

## Checksums

- Merged binary SHA256:
  `2b0c572e30711dca074bae65b0d3d3afb3c60ef7a321e13b1d152a769e9521ad`
- Signed OTA SHA256:
  `5098fa460d5f2afc52bc0b83b09bd2d42bd3a57a9e106f177aed2d661cc517aa`

## Build Notes

Manual builds require the complete `source/ESP32-S3/V1.4/` folder with its
custom `partitions.csv`, `build_opt.h`, and `ota_public_key.h` files.
