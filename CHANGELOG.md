# Changelog

All public release changes are tracked here.

## ESP32-S3 Dual-Reader V1.2

- Adds a filament-loaded safety lock: tag data is sent only after a fresh
  sensor query confirms that the assigned Tool Head has no filament loaded
- Pauses RFID polling independently for a spool position whose Tool Head
  reports filament detected; the other local reader remains available
- Reduces motion polling for a loaded channel to approximately every five
  seconds and resumes normal behavior after unloading
- Retains the hardware-confirmed fast QIDI and OpenSpool read path, BSSID/RSSI
  visibility, and the FreeRTOS dual-reader architecture

Source and binary:

- [source/ESP32-S3/V1.2/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_2.ino](./source/ESP32-S3/V1.2/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_2.ino)
- [firmware/ESP32-S3/V1.2/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_2.ino.merged.bin](./firmware/ESP32-S3/V1.2/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_2.ino.merged.bin)

## ESP32-C3 Single-Reader V2.2

- Adds the filament-loaded safety lock with a fresh sensor query before any
  tag-triggered SET
- Pauses PN532 polling while filament is detected and reduces loaded-state
  motion polling to approximately every five seconds
- Keeps printer polling active after a successful tag read on the single-core
  firmware so the safety lock can engage while a tag remains in position
- Retains the V2.1 PN532, QIDI, OpenSpool, and BSSID behavior

Source and binary:

- [source/ESP32-C3/V2.2/U1_Argus_Remote_RFID_ESP32-C3_V2_2.ino](./source/ESP32-C3/V2.2/U1_Argus_Remote_RFID_ESP32-C3_V2_2.ino)
- [firmware/ESP32-C3/V2.2/U1_Argus_Remote_RFID_ESP32-C3_V2_2.ino.merged.bin](./firmware/ESP32-C3/V2.2/U1_Argus_Remote_RFID_ESP32-C3_V2_2.ino.merged.bin)

## Installer Maintenance

- Removes the web-installer erase prompt from all selectable firmware
  manifests because it did not provide a reliable erase workflow

## ESP32-S3 Dual-Reader V1.1

- Shows the currently connected BSSID in the dashboard Network tile
- Adds a cached list of visible 2.4 GHz BSSIDs for the configured SSID,
  including RSSI and channel, to the dashboard and standard serial output
- Refreshes Wi-Fi visibility outside the NFC task and defers scans while a tag
  is active, preserving the confirmed fast OpenSpool and QIDI read path
- Fixes persistence of the optional additional dual-reader Tool Head
  assignments by using ESP32 Preferences keys within the NVS length limit
- Retains the V1.0 dual PN532 HSU reader architecture and Core 0/Core 1
  FreeRTOS split
- Documents the tested `4MB (32Mb)` flash and `Huge APP (3MB No OTA/1MB
  SPIFFS)` Arduino IDE selections for the S3 board

Source and binary:

- [source/ESP32-S3/V1.1/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_1.ino](./source/ESP32-S3/V1.1/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_1.ino)
- [firmware/ESP32-S3/V1.1/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_1.ino.merged.bin](./firmware/ESP32-S3/V1.1/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_1.ino.merged.bin)

## ESP32-C3 Single-Reader V2.1

- Adds optional configuration of two preferred Wi-Fi BSSIDs for setups with multiple routers, repeaters, or mesh access points broadcasting one SSID
- Connects through visible preferred 2.4 GHz access points in configured order and uses another access point of the SSID only when neither preferred BSSID is visible
- Shows the currently connected BSSID in the dashboard Network tile
- Mirrors standard ESP32-C3 firmware logs to USB-Serial/JTAG when required by the board profile
- Retains the V2.0 PN532, OpenSpool, and QIDI RFID read/write implementation without behavior changes
- Reorganizes public source and merged binaries into hardware-specific `ESP32-C3` and `ESP32-S3` folders

Source and binary:

- [source/ESP32-C3/V2.1/U1_Argus_Remote_RFID_ESP32-C3_V2_1.ino](./source/ESP32-C3/V2.1/U1_Argus_Remote_RFID_ESP32-C3_V2_1.ino)
- [firmware/ESP32-C3/V2.1/U1_Argus_Remote_RFID_ESP32-C3_V2_1.ino.merged.bin](./firmware/ESP32-C3/V2.1/U1_Argus_Remote_RFID_ESP32-C3_V2_1.ino.merged.bin)

## Documentation And Installer Maintenance

- Converts the current ESP32-C3 and ESP32-S3 user guides to English-only documentation
- Simplifies the current browser installer to an English-only interface while retaining hardware-specific release selection

## ESP32-S3 Dual-Reader V1.0

- Publishes the first ESP32-S3 release, built from the hardware-tested `V0.27` development baseline
- Adds the ESP32-S3 N16R8 hardware variant with two PN532 readers over independent HSU/UART connections
- Keeps the hardware-confirmed fast NFC path for both OpenSpool/NTAG and QIDI/MIFARE Classic tags
- Uses a FreeRTOS task pinned to Core 0 for NFC polling while the Arduino main loop handles web and printer communication on Core 1
- Adds optional configuration of two preferred Wi-Fi BSSIDs, with ordered priority and SSID fallback only when neither preferred access point is visible
- Extends the browser web installer with an ESP32-S3 target and hardware-specific release selection lists

Source and binary:

- [source/ESP32-S3/V1.0/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_0.ino](./source/ESP32-S3/V1.0/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_0.ino)
- [firmware/ESP32-S3/V1.0/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_0.ino.merged.bin](./firmware/ESP32-S3/V1.0/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_0.ino.merged.bin)

## V2.0

- Adds QIDI MIFARE Classic 1K tag support with vendor, material, and color mapping
- Includes compact built-in QIDI Plus4 defaults
- Adds optional setup upload for reduced `officiall_filas_list.cfg` data
- Stores uploaded QIDI material/vendor mappings persistently in ESP32 `Preferences`
- Speeds up QIDI reads through raw PN532 HSU authentication and block reads
- Speeds up large OpenSpool reads with larger NTAG `FAST_READ` chunks
- Supports larger NTAG/OpenSpool payloads such as tags with opacity, weight, and multiple extra colors
- Parses OpenSpool JSON once and reuses the mapped fields for printer payloads, comparisons, and dashboard state
- Keeps standard serial logs active while verbose debug remains disabled by default
- Keeps the device UI and current web installer English-only for a compact, consistent public interface

Release source:

- [source/ESP32-C3/V2.0/U1_Argus_Remote_RFID_V2.0.ino](./source/ESP32-C3/V2.0/U1_Argus_Remote_RFID_V2.0.ino)

Firmware folder:

- [firmware/ESP32-C3/V2.0](./firmware/ESP32-C3/V2.0/)

## V1.3

- Allows the Snapmaker U1 printer address to be configured as IPv4 address or hostname/mDNS name, for example `192.168.1.120` or `u1.local`
- Resolves `.local` printer names through mDNS before building printer API URLs
- Adds a setup-page prefill button for additional reader URLs based on this reader's mDNS name and selected Tool Head
- Preserves existing additional-reader entries during prefill and only fills empty slots
- Keeps setup values in ESP32 `Preferences` and adds a config-version marker for future migrations
- Exposes printer address and address type in the local state API for diagnostics

Release source:

- [source/ESP32-C3/V1.3/U1_Argus_Remote_RFID_V1.3.ino](./source/ESP32-C3/V1.3/U1_Argus_Remote_RFID_V1.3.ino)

Firmware folder:

- [firmware/ESP32-C3/V1.3](./firmware/ESP32-C3/V1.3/)

## V1.2

- Improves multi-reader startup reliability when several ESP32-C3 readers are powered at the same time
- Gives configured Wi-Fi up to 30 seconds before setup hotspot fallback starts
- Adds a device-specific setup hotspot SSID suffix, for example `U1-Argus-Setup-A3F2`
- Keeps checking the configured Wi-Fi while setup hotspot is active and automatically returns to station mode when Wi-Fi becomes available
- Reduces printer-side polling load in multi-reader setups:
  - idle polling reads only this reader's own `filament_motion_sensor eX_filament=filament_detected`
  - full `filament_detect=info` is read on boot sync, feeder activity, valid tag reads, SET verification, and slow sync
- Improves filament update confidence:
  - reads fresh printer info before comparing a valid tag when the cached state is stale
  - verifies successful `filament_detect/set` updates after a short delay
  - retries up to two times if the printer still reports different channel data

Release source:

- [source/ESP32-C3/V1.2/U1_Argus_Remote_RFID_V1.2.ino](./source/ESP32-C3/V1.2/U1_Argus_Remote_RFID_V1.2.ino)

Firmware folder:

- [firmware/ESP32-C3/V1.2](./firmware/ESP32-C3/V1.2/)

## V1.1

- Improves dashboard navigation for multi-reader setups:
  - current reader URL is shown in the main status chip row
  - current reader Tool Head is shown in the setup/navigation tile
  - optional additional reader links can be named by Tool Head
- Clarifies Tool Head numbering as `Tool Head 1..4` with matching internal `Channel 0..3`
- Fixes loading of saved additional reader IPs/URLs and their Tool Head assignment
- Reduces printer status load in multi-reader setups:
  - each reader only queries its own `filament_motion_sensor eX_filament`
  - status polls are staggered by Tool Head to avoid four readers polling at once
- Improves webhook resend logic:
  - a tag is skipped only after the printer confirms matching channel data
  - mismatching printer state allows the same tag payload to be resent
- Improves NFC behavior for moving spools and extended OpenSpool tags:
  - NFC polling is prioritized before slower printer status refreshes
  - `FAST_READ` experiment was reverted because some PN532/tag combinations were slower
  - tag detection timing is tuned back toward V1.0 reliability
  - pages `4..6` are reused from the capability-container read instead of being read twice
  - extended OpenSpool JSON is no longer parsed twice before sending
- Starts the setup hotspot earlier while a slow Wi-Fi connection is still being attempted
- Adds serial timing output for complete NFC JSON reads

Release source:

- [source/ESP32-C3/V1.1/U1_Argus_Remote_RFID_V1.1.ino](./source/ESP32-C3/V1.1/U1_Argus_Remote_RFID_V1.1.ino)

Firmware folder:

- [firmware/ESP32-C3/V1.1](./firmware/ESP32-C3/V1.1/)

## V1.0

- First public release of `U1 Argus Remote RFID`
- ESP32-C3 Super Mini + PN532 in `HSU/UART`
- OpenSpool tag read support with raw PN532 HSU transport
- Captive portal onboarding via `U1-Argus-Setup`
- English and German web UI support
- Live dashboard with printer query, tag state, webhook state, and multi-reader buttons
- Web installer prepared for a single public firmware line `V1.0`

Release source:

- [source/ESP32-C3/V1.0/U1_Argus_Remote_RFID_V1_0.ino](./source/ESP32-C3/V1.0/U1_Argus_Remote_RFID_V1_0.ino)
