# ESP32-S3 Dual-Reader V1.5

ESP32-S3 Dual-Reader V1.5 keeps the fast V1.4 RFID baseline and focuses on
printer-side compatibility, safer card identity handling, and post-update
reader recovery.

## Highlights

- Sends `CARD_UID` with successfully parsed QIDI and OpenSpool tag payloads.
- Adds `CARD_TYPE` for supported printer firmware:
  - `M1` for QIDI / MIFARE Classic tags
  - `NTAG` for OpenSpool / NTAG / MIFARE Ultralight tags
- Automatically retries without `CARD_TYPE` when deployed printer firmware
  rejects that field with a compatibility error.
- Keeps `CARD_UID` in the fallback request so future printer-side integrations
  can still identify the physical card.
- Removes automatic UID-only sends after transient QIDI/OpenSpool read or parse
  failures, avoiding accidental clearing of existing printer filament data.
- Adds a PN532 soft reinitialization path after OTA/reboot so readers can
  recover without a physical power cycle when a module is slow to answer.
- Adds implementation notes for reusing the printer-send behavior in the
  BoxRFID Touch project.

## Firmware Files

- First-install / web-installer binary:
  `firmware/ESP32-S3/V1.5/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_5.ino.merged.bin`
- Signed OTA update:
  `firmware/ESP32-S3/V1.5/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_5.ota.signed.bin`
- Arduino source:
  `source/ESP32-S3/V1.5/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_5.ino`

## Notes

The ESP32-S3 V1.5 build still requires the custom 16 MB dual-slot partition
layout from `source/ESP32-S3/V1.5/partitions.csv`. When building from Arduino
IDE, open the complete `source/ESP32-S3/V1.5/` folder so `partitions.csv`,
`build_opt.h`, and `ota_public_key.h` are included.
