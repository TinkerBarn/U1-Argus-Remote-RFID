# User Guide: ESP32-C3 Single-Reader

This guide describes the ESP32-C3 version of the U1 Argus Remote RFID reader.
It controls one PN532 reader and sends detected filament data over Wi-Fi to an
assigned Snapmaker U1 Tool Head.

## Requirements

- ESP32-C3 Super Mini with one PN532 module in HSU/UART mode
- Installed ESP32-C3 firmware, currently `V2.2`
- Snapmaker U1 with Extended Firmware
- **Filament Detection** set to **External** in the printer firmware settings
- A **2.4 GHz** Wi-Fi network

The ESP32-C3 supports `802.11 b/g/n` Wi-Fi in the 2.4 GHz band. It cannot
connect to a 5 GHz-only network. If the router uses a shared network name for
2.4 GHz and 5 GHz, an accessible 2.4 GHz network must still be available.

## Printable Holder Options

The MakerWorld model below includes **ESP32-C3 plus PN532 holders** that can
be mounted on the SH02 base plate, as well as universal ESP32-C3 holder
variants for custom reader installations:

- [Dual RFID Reader for SH02 Dryer and Snapmaker U1](https://makerworld.com/en/models/2847054-dual-rfid-reader-for-sh02-dryer-and-snapmaker-u1#profileId-3175165)

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
| Preferred BSSID 1 / 2 | Optional priority for nearby 2.4 GHz access points or repeaters broadcasting the same SSID |
| Hostname | Reader name on the local network, for example `argus-left`; accessible as `http://argus-left.local` |
| Printer IP / Hostname | IP address or mDNS hostname of the Snapmaker U1 |
| Printer Port | Printer API port; default is `7125` |
| Tool Head | Channel updated by the local PN532 reader |
| Additional Readers | Optional links to separate reader devices for dashboard navigation |
| QIDI config upload | Updates QIDI material and manufacturer names |
| QIDI config reset | Restores the built-in QIDI default mappings |

The C3 version operates exactly one local RFID reader. To monitor multiple
spools, configure multiple C3 reader devices and link their dashboards.

## Preferred BSSIDs

A **BSSID** is the MAC address of a specific Wi-Fi access point or repeater
radio, for example `AA:BB:CC:DD:EE:FF`. It is particularly useful in a more
complex Wi-Fi setup where multiple routers, repeaters, or mesh nodes broadcast
the same SSID. Configuring one or two nearby BSSIDs helps make sure this reader
connects through the 2.4 GHz radio close to the printer instead of a distant
access point.

When BSSIDs are configured, the firmware:

1. Looks for the configured SSID on preferred BSSID 1, then BSSID 2.
2. Connects through the first visible preferred access point in that order.
3. Uses another access point with the same SSID only if neither preferred
   BSSID is visible.

Enter only a BSSID for a **2.4 GHz** Wi-Fi radio. The ESP32-C3 cannot connect
to a 5 GHz radio.

### List Available BSSIDs On Windows

Open PowerShell or Command Prompt:

```powershell
netsh wlan show networks mode=bssid
```

For a shorter PowerShell view:

```powershell
netsh wlan show networks mode=bssid | Select-String 'SSID|BSSID|Signal|Channel'
```

Locate your SSID and choose a BSSID on a 2.4 GHz channel. In most regions,
2.4 GHz channel numbers are from `1` through `13`.

### List Available BSSIDs On macOS

On macOS versions that still include Apple's `airport` command, run:

```sh
/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport -s
```

If that command is unavailable, open **Wireless Diagnostics** and choose
**Window > Scan**. Holding `Option` while clicking the Wi-Fi icon shows the
BSSID of the access point to which the Mac is currently connected.

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
| Network | SSID, IP address, hostname, signal strength, currently connected BSSID, and firmware version |
| Additional Readers | Quick access to configured additional reader dashboards |

A tag is sent to the printer only after valid data has been read and an update
is required for the assigned channel.

## Filament-Loaded Safety Lock

Starting with `V2.2`, the reader protects the active printer filament
assignment after filament has reached the assigned Tool Head:

- Immediately before sending different tag data, the reader checks the
  printer's filament sensor.
- Tag data is sent only while the sensor reports **No filament**.
- While the sensor reports **Filament detected**, RFID polling pauses and
  printer motion status is checked approximately every five seconds.
- When filament is removed, normal RFID detection and update behavior resumes.

This prevents a nearby, changed, or incorrectly read tag from overwriting the
filament data for a spool that is already loaded for printing.

## Initial Setup

1. Start the reader and connect to its `U1-Argus-Setup-XXXX` hotspot.
2. Open the captive portal, or manually open `http://192.168.4.1`.
3. Configure Wi-Fi, printer address, Tool Head, and optional preferred BSSIDs
   or additional reader dashboard links.
4. Save the configuration and let the reader reboot.

## Normal Operation

1. Start the printer and the configured reader.
2. Load a filament spool with an attached OpenSpool or QIDI tag into the
   dryer/reader position.
3. The reader detects the tag automatically and transfers the filament
   information to the assigned printer Tool Head when required.

Once configured, there is normally nothing else to operate: load filament and
print.

## Changing Settings Or Viewing Details

Open the configured reader URL, for example `http://argus-left.local`, or its
displayed IP address only when you want to view dashboard details or change
setup values.

## Troubleshooting

For connection problems, first verify the 2.4 GHz Wi-Fi network and that any
preferred BSSID belongs to the configured SSID and is visible nearby. Then
check the printer IP or mDNS name, port `7125`, and the printer setting
**Filament Detection: External**.

## Reference

- [Espressif ESP32-C3 product overview](https://www.espressif.com/en/products/socs/esp32-c3)
- [Microsoft documentation for `netsh wlan`](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/netsh-wlan)
