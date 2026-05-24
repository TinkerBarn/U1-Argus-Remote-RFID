# User Guide: ESP32-C3 Single-Reader

This guide describes the ESP32-C3 version of the U1 Argus Remote RFID reader.
It controls one PN532 reader and sends detected filament data over Wi-Fi to an
assigned Snapmaker U1 Tool Head.

## Requirements

- ESP32-C3 Super Mini with one PN532 module in HSU/UART mode
- Installed ESP32-C3 firmware, currently `V2.0`
- Snapmaker U1 with Extended Firmware
- **Filament Detection** set to **External** in the printer firmware settings
- A **2.4 GHz** Wi-Fi network

The ESP32-C3 supports `802.11 b/g/n` Wi-Fi in the 2.4 GHz band. It cannot
connect to a 5 GHz-only network. If the router uses a shared network name for
2.4 GHz and 5 GHz, an accessible 2.4 GHz network must still be available.

## First Setup

On first boot, or after stored settings have been cleared, the device opens a
setup hotspot:

- SSID: `U1-Argus-Setup-XXXX`
- Configuration page: `http://192.168.4.1`

Connect a phone or computer to this hotspot. If the captive portal does not
open automatically, open the configuration address manually.

## Setup Web Interface

| Setting | Purpose |
| --- | --- |
| Wi-Fi SSID / Password | Access to the 2.4 GHz Wi-Fi network where the printer is reachable |
| Hostname | Reader name on the local network, for example `argus-left`; accessible as `http://argus-left.local` |
| Printer IP / Hostname | IP address or mDNS hostname of the Snapmaker U1 |
| Printer Port | Printer API port; default is `7125` |
| Tool Head | Channel updated by the local PN532 reader |
| Additional Readers | Optional links to separate reader devices for dashboard navigation |
| QIDI config upload | Updates QIDI material and manufacturer names |
| QIDI config reset | Restores the built-in QIDI default mappings |

The C3 version operates exactly one local RFID reader. To monitor multiple
spools, configure multiple C3 reader devices and link their dashboards.

## Supported Tags

- **OpenSpool / NTAG**: Reads OpenSpool NDEF data and sends the mapped
  material information to the assigned Tool Head.
- **QIDI / MIFARE Classic**: Authenticates the QIDI data block, reads
  material, manufacturer, and color codes, and updates the printer channel.

## QIDI `officiall_filas_list.cfg`

The firmware already contains compact default mappings for common QIDI Plus4
materials. The optional QIDI configuration file
`officiall_filas_list.cfg` can be uploaded in Setup. The double `l` spelling
is the filename expected by the firmware.

The upload:

- updates the mapping from QIDI material numbers to material names,
- updates the mapping from manufacturer numbers to manufacturer names,
- is stored persistently on the ESP32,
- does not reprogram an RFID tag or modify printer firmware.

Use **Reset QIDI config** to remove uploaded mappings and return to the
built-in defaults.

## Dashboard

The dashboard displays reader operation and the most recently confirmed
filament data from the printer.

| Area | Information Shown |
| --- | --- |
| Status bar | Wi-Fi connection, printer reachability, and tag status |
| Printer Tool Head | Assigned channel, manufacturer, material type, subtype, color, hotend temperatures, bed temperature, filament sensor, and official status when supplied by the printer |
| Tag Reader | Last valid tag, UID, source (`OpenSpool` or `QIDI`), read manufacturer, material, color, and temperature values |
| Set/Webhook result | Whether tag data was successfully transferred to the printer, HTTP status, and last sent data |
| Network | SSID, IP address, hostname, signal strength, printer target, and firmware version |
| Additional Readers | Quick access to configured additional reader dashboards |

A tag is sent to the printer only after valid data has been read and an update
is required for the assigned channel.

## Normal Operation

1. Start the printer and reader on the same 2.4 GHz Wi-Fi network.
2. Open the dashboard using the configured hostname or displayed IP address.
3. Place an OpenSpool or QIDI tag at the PN532 reader.
4. Check that the Tag Reader data and Printer Tool Head data match.

For connection problems, first verify the 2.4 GHz Wi-Fi network, printer IP or
mDNS name, port `7125`, and the printer setting
**Filament Detection: External**.

## Reference

- [Espressif ESP32-C3 product overview](https://www.espressif.com/en/products/socs/esp32-c3)
