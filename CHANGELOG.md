# Changelog

All public release changes are tracked here.

## V1.1

- Improves dashboard navigation for multi-reader setups:
  - current reader URL is shown in the main status chip row
  - current reader Tool Head is shown in the setup/navigation tile
  - optional additional reader links can be named by Tool Head
- Clarifies Tool Head numbering as `Tool Head 1..4` with matching internal `Channel 0..3`
- Fixes loading of saved additional reader IPs/URLs and their Tool Head assignment
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
