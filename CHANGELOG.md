# Changelog

All public release changes are tracked here.

## V1.1

- Smaller firmware by removing the Adafruit PN532 dependency
- Raw PN532 HSU/UART setup and status handling
- Persistent additional-reader setup fields for multi-reader dashboards
- Standard serial boot, Wi-Fi, mDNS, IP, and PN532 status output remains available
- Web installer updated to the current public `V1.1` firmware binary

Release source:

- [releases/V1.1/U1_Argus_Remote_RFID_V1.1.ino](./releases/V1.1/U1_Argus_Remote_RFID_V1.1.ino)

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

- [releases/V1.0/U1_Argus_Remote_RFID_V1.0.ino](./releases/V1.0/U1_Argus_Remote_RFID_V1.0.ino)

Firmware folder:

- [firmware/V1.0](./firmware/V1.0/)
