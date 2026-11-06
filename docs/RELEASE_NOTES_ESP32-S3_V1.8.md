# ESP32-S3 Dual-Reader V1.8

ESP32-S3 Dual-Reader V1.8 adds an optional OpenSpool UID-only workflow for
paxx12 installations that use Spoolman as the source of filament and spool
information. It also makes empty NTAG NDEF records usable as UID lookup tags
in the normal reading mode.

## Highlights

- Adds a persistent **OpenSpool UID-only Mode** setting, disabled by default.
- When enabled, every tag readable by the PN532 sends only its `CARD_UID` and
  assigned printer channel. QIDI authentication and NDEF/OpenSpool content
  reads are skipped.
- Shows the requirements directly in Setup: a Snapmaker U1 running paxx12 with
  Spoolman enabled, a reachable Spoolman server, and a matching filament/spool
  record for the detected UID.
- Enables faster handling and otherwise unknown tags when their UID is readable
  and registered in Spoolman.
- Clearly states the limitation: without a matching Spoolman profile, the UID
  cannot be resolved to filament metadata.
- When UID-only mode is disabled, normal QIDI and OpenSpool reading remains
  active and sends the tag's complete mapped filament data together with its UID.
- Adds a targeted UID-only fallback for a valid NTAG NDEF TLV with `Len=0`.
  Failed memory reads, missing NDEF TLVs, malformed records, and unsupported
  non-empty records do not trigger the fallback.
- Retains the filament-loaded safety lock, signed OTA verification, moving-tag
  polling behavior, DebugSerial live dashboard panel, and printer self-heal.

## Firmware Files

- First-install / web-installer binary:
  `firmware/ESP32-S3/V1.8/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_8.ino.merged.bin`
- Signed OTA update:
  `firmware/ESP32-S3/V1.8/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_8.ota.signed.bin`
- Arduino source:
  `source/ESP32-S3/V1.8/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_8.ino`

SHA-256:

- merged binary: `42938b9f525bd931ca1bf47baefbff54a98da3433c99f530d21d198aeb3cb840`
- signed OTA: `c15692468d8cdef14eee9a8ae6ef4c12ae54db3c65adad6c19200f7344a863c2`

## Installation

Existing V1.3 and later installations can install the signed OTA file from
Setup. V1.0-V1.2 require one browser/USB installation of V1.3 or later to gain
the permanent dual-slot OTA layout. A first installation can use the browser
web installer and the merged binary.

The ESP32-S3 V1.8 build requires an N16R8 board with 16 MB flash and the custom
dual-slot layout from `source/ESP32-S3/V1.8/partitions.csv`. Open the complete
V1.8 source folder in Arduino IDE so `partitions.csv`, `build_opt.h`,
`ota_public_key.h`, `debug_log_buffer.h`, and `debug_log_panel.h` are included.
