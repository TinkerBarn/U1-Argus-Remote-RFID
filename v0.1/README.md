# U1 Argus Remote RFID (V0.1)

Arduino sketch for **ESP32-C3 + PN532 (HSU/UART)**.

## Features
- AP provisioning with web form (open hotspot)
- Stores Wi-Fi, hostname, printer IP/port, and toolhead channel (0..3)
- mDNS hostname in STA mode
- Reads NFC tags and accepts only OpenSpool JSON tags
- Sends mapped payload to Snapmaker U1 endpoint: `/printer/filament_detect/set`
- Sends clear message when no valid OpenSpool tag is detected for a timeout period

## File
- `U1_Argus_Remote_RFID_V0_1.ino`

## Dependencies
- Adafruit PN532
- ArduinoJson

## Notes
- This V0.1 deduplicates by comparing with the last successfully sent payload.
- Printer-side state query logic can be added in a future release.
