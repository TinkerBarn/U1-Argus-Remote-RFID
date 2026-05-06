# Changelog

All public release changes are tracked here.

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
- Keeps the device UI English-only to preserve ESP32-C3 flash headroom; the web installer remains English/German

Release source:

- [source/V2.0/U1_Argus_Remote_RFID_V2.0.ino](./source/V2.0/U1_Argus_Remote_RFID_V2.0.ino)

Firmware folder:

- [firmware/V2.0](./firmware/V2.0/)

## V1.3

- Allows the Snapmaker U1 printer address to be configured as IPv4 address or hostname/mDNS name, for example `192.168.1.120` or `u1.local`
- Resolves `.local` printer names through mDNS before building printer API URLs
- Adds a setup-page prefill button for additional reader URLs based on this reader's mDNS name and selected Tool Head
- Preserves existing additional-reader entries during prefill and only fills empty slots
- Keeps setup values in ESP32 `Preferences` and adds a config-version marker for future migrations
- Exposes printer address and address type in the local state API for diagnostics

Release source:

- [source/V1.3/U1_Argus_Remote_RFID_V1.3.ino](./source/V1.3/U1_Argus_Remote_RFID_V1.3.ino)

Firmware folder:

- [firmware/V1.3](./firmware/V1.3/)

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

- [source/V1.2/U1_Argus_Remote_RFID_V1.2.ino](./source/V1.2/U1_Argus_Remote_RFID_V1.2.ino)

Firmware folder:

- [firmware/V1.2](./firmware/V1.2/)

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

- [source/V1.1/U1_Argus_Remote_RFID_V1.1.ino](./source/V1.1/U1_Argus_Remote_RFID_V1.1.ino)

Firmware folder:

- [firmware/V1.1](./firmware/V1.1/)

## V1.0

- First public release of `U1 Argus Remote RFID`
- ESP32-C3 Super Mini + PN532 in `HSU/UART`
- OpenSpool tag read support with raw PN532 HSU transport
- Captive portal onboarding via `U1-Argus-Setup`
- English and German web UI support
- Live dashboard with printer query, tag state, webhook state, and multi-reader buttons
- Web installer prepared for a single public firmware line `V1.0`

Release source:

- [source/V1.0/U1_Argus_Remote_RFID_V1_0.ino](./source/V1.0/U1_Argus_Remote_RFID_V1_0.ino)
