# User Guide: ESP32-S3 Dual-Reader

This guide describes the ESP32-S3 N16R8 `V1.1` version of the U1 Argus Remote
RFID reader. One controller operates two PN532 readers for the left and right
spool positions and sends detected data to their assigned Snapmaker U1 Tool
Heads.

## Requirements

- ESP32-S3 N16R8 with two PN532 modules in HSU/UART mode
- Installed ESP32-S3 firmware `V1.1`
- Snapmaker U1 with Extended Firmware
- **Filament Detection** set to **External** in the printer firmware settings
- A **2.4 GHz** Wi-Fi network

The ESP32-S3 supports `802.11 b/g/n` Wi-Fi in the 2.4 GHz band. It cannot
connect to a 5 GHz-only network, even when other nearby devices preferentially
use 5 GHz.

## What The S3 Version Does Differently

The S3 release combines two local readers in one controller:

- **Left spool** and **Right spool** each have their own PN532 module and
  Tool Head assignment.
- A FreeRTOS task on Core 0 services the NFC readers.
- The Arduino main loop on Core 1 services the web interface, Wi-Fi, and
  printer communication.
- The validated fast PN532 HSU read path supports OpenSpool/NTAG and
  QIDI/MIFARE Classic tags.

This separation keeps blocking NFC polling away from web and network
processing. `V1.1` is functionally based on the hardware-tested `V0.29`
development baseline.

## First Setup

On first boot, or after stored settings have been cleared, the device opens:

- SSID: `U1-Argus-Setup-XXXX`
- Configuration page: `http://192.168.4.1`

Connect a phone or computer to the hotspot. If the captive portal does not
open automatically, open the configuration address manually.

## Setup Web Interface

| Setting | Purpose |
| --- | --- |
| Wi-Fi SSID / Password | Access to the 2.4 GHz Wi-Fi network with the Snapmaker U1 |
| Preferred BSSID 1 / 2 | Optional priority for particular 2.4 GHz access points broadcasting the same SSID |
| Hostname | Local reader name, for example `argus-dual`; accessible as `http://argus-dual.local` |
| Printer IP / Hostname | IP address or mDNS hostname of the Snapmaker U1 |
| Printer Port | API port; default is `7125` |
| Left spool Tool Head | Printer channel for the local left PN532 |
| Right spool Tool Head | Printer channel for the local right PN532 |
| Additional dual reader | Optional link to another two-spool reader with its left/right Tool Heads |
| Serial debug log | Additional diagnostic output; it can slow tag detection and is disabled by default |
| QIDI config upload / reset | Updates or removes custom QIDI material and manufacturer mappings |

With debug disabled, essential serial information remains available, including
the setup hotspot, Wi-Fi status, a tag detection line, and printer transfer
status. `V1.1` additionally reports visible 2.4 GHz BSSIDs for the configured
SSID with their RSSI values when the list is initially scanned or changes.

## Preferred BSSIDs

A **BSSID** is the MAC address of a specific Wi-Fi radio, for example
`AA:BB:CC:DD:EE:FF`. This is useful when several access points or mesh nodes
broadcast the same SSID and the reader should prefer a nearby or stable
2.4 GHz access point.

When BSSIDs are configured, the firmware connects in this order:

1. It scans visible networks for the configured SSID and BSSID 1.
2. If BSSID 1 is not visible, it tries BSSID 2.
3. Only when neither preferred access point is visible does it use another
   visible access point with the same SSID.

Enter only the BSSID of a **2.4 GHz** network. The ESP32-S3 cannot use the
BSSID of a 5 GHz radio.

### List Available BSSIDs On Windows

Open PowerShell or Command Prompt:

```powershell
netsh wlan show networks mode=bssid
```

For a shorter PowerShell view:

```powershell
netsh wlan show networks mode=bssid | Select-String 'SSID|BSSID|Signal|Channel'
```

Find your SSID and select the BSSID of a 2.4 GHz channel. In most regions,
2.4 GHz Wi-Fi channels are numbered from `1` through `13`.

### List Available BSSIDs On macOS

On macOS versions that still include the older `airport` system utility, run
a scan in Terminal:

```sh
/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport -s
```

Apple does not include this private utility on every newer macOS version. If
the command is unavailable, open **Wireless Diagnostics** and select
**Window > Scan**, or hold `Option` while clicking the Wi-Fi icon to view the
BSSID of the currently connected access point.

## Supported Tags

- **OpenSpool / NTAG**: Reads OpenSpool NDEF content through the respective
  reader and transfers material data to its Tool Head.
- **QIDI / MIFARE Classic**: Authenticates and reads the QIDI data block,
  maps material, manufacturer, and color codes, and updates the relevant
  channel.

## QIDI `officiall_filas_list.cfg`

The firmware includes compact default mappings for common QIDI Plus4
materials. The optional QIDI file `officiall_filas_list.cfg` can be uploaded
in Setup. The double `l` spelling is the filename expected by the firmware.

The upload persistently updates:

- material numbers and their displayed material names,
- manufacturer numbers and their displayed manufacturer names.

It does not overwrite RFID tags or install anything on the printer. Resetting
the QIDI configuration removes uploaded mappings and activates the built-in
defaults again.

## Dashboard

The S3 dashboard displays both local spool positions separately.

| Area | Information Shown |
| --- | --- |
| Status bar | Wi-Fi connection, printer reachability, and current tag status |
| Left/Right Printer Tool Head | Assigned channel and printer-confirmed manufacturer, material, color, temperature, sensor, and official data |
| Left/Right Tag Reader | Most recently read UID, tag source (`OpenSpool` or `QIDI`), manufacturer, material, color, and temperature values for each spool |
| Set result | Result of the last update to the relevant printer channel, including HTTP status |
| Network | SSID, IP address, hostname, signal strength, currently connected BSSID, firmware version, and visible 2.4 GHz BSSIDs for the configured SSID with RSSI and channel |
| Additional Reader | Quick access to an optional additional two-spool dashboard |

## Initial Setup

1. Start the reader and connect to its `U1-Argus-Setup-XXXX` hotspot.
2. Open the captive portal, or manually open `http://192.168.4.1`.
3. Configure Wi-Fi, printer address, left/right Tool Heads, and optional
   preferred BSSIDs or additional reader dashboard.
4. Save the configuration and let the reader reboot.

## Normal Operation

1. Start the printer and the configured reader.
2. Load a filament spool with an attached OpenSpool or QIDI tag into the
   left or right dryer/reader position.
3. The reader detects the tag automatically and transfers the filament
   information to the assigned printer Tool Head when required.

Once configured, there is normally nothing else to operate: load filament and
print.

## Changing Settings Or Viewing Details

Open the configured reader URL, for example `http://argus-dual.local`, or its
displayed IP address only when you want to view dashboard details or change
setup values. Keep debug logging disabled for normal use and enable it only
temporarily for troubleshooting.

## Manual Arduino IDE Upload Settings

For the tested ESP32-S3 dual-reader board, use:

| Setting | Value |
| --- | --- |
| Board | `ESP32S3 Dev Module` |
| CPU Frequency | `240MHz (WiFi)` |
| Flash Mode | `QIO 80MHz` |
| Flash Size | `4MB (32Mb)` |
| Partition Scheme | `Huge APP (3MB No OTA/1MB SPIFFS)` |
| PSRAM | `OPI PSRAM` |
| Arduino Runs On / Events Run On | `Core 1` / `Core 1` |
| USB CDC On Boot | `Enabled` |
| USB Mode / Upload Mode | `Hardware CDC and JTAG` / `UART0 / Hardware CDC` |

Keep `Erase All Flash Before Sketch Upload` disabled for updates if existing
settings should remain stored. Although the module is sold as N16R8, the
tested screw-terminal expansion-board unit reported a 4 MB accessible flash
window at boot. Do not select `16M Flash (3MB APP/9.9MB FATFS)` for this
tested board configuration: it caused a boot-loop partition validation
failure.

## Troubleshooting

For connection problems, first verify that a configured preferred BSSID
belongs to your 2.4 GHz SSID and is visible at the installation location.
Then check the printer address, port `7125`, and
**Filament Detection: External**.

## References

- [Espressif ESP32-S3 product overview](https://www.espressif.com/en/products/socs/esp32-s3/)
- [Espressif ESP32 chip comparison with Wi-Fi band and CPU information](https://docs.espressif.com/projects/esp-idf/en/v5.0.3/esp32c3/hw-reference/chip-series-comparison.html)
- [Microsoft documentation for `netsh wlan`](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/netsh-wlan)
