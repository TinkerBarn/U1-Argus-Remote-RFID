# U1 Argus Remote RFID

<p align="center">
  <img src="./assets/branding/u1-argus-hero-banner.png" alt="U1 Argus Remote RFID hero banner" width="100%">
</p>

<p align="center">
  Remote OpenSpool RFID reader for Snapmaker U1 with ESP32-C3 Super Mini and PN532.
</p>

<p align="center">
  Reads OpenSpool NFC tags, maps them to the Snapmaker U1 external filament workflow, and shows a beautiful local dashboard with live channel and tag status.
</p>

<p align="center">
  <a href="https://tinkerbarn.github.io/U1-Argus-Remote-RFID/"><strong>Open Web Installer</strong></a>
  ·
  <a href="./index.html"><strong>Installer Source In Repo</strong></a>
</p>

<p align="center">
  <a href="https://ko-fi.com/H2H41XBKJ6">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Support on Ko-fi">
  </a>
</p>

---

## What This Reader Can Do

- Read **OpenSpool Standard** RFID/NFC tags through a **PN532** in **HSU/UART** mode
- Send mapped filament information to a **Snapmaker U1** over the external filament-detection API
- Offer a built-in **setup hotspot** with captive portal for first configuration
- Persist Wi-Fi, printer, language, and dashboard-reader settings in ESP32 `Preferences`
- Serve a local **dashboard** that shows:
  - current printer channel information
  - last valid tag information
  - last webhook result
  - quick buttons to jump between up to **4 readers**
- Support **English and German** in setup, captive portal, dashboard, and web installer

---

## Before You Start

This project is intended for **Snapmaker U1** together with the **Extended Firmware by paxx12**:

- [paxx12 / SnapmakerU1-Extended-Firmware (develop)](https://github.com/paxx12/SnapmakerU1-Extended-Firmware/tree/develop)

Required printer-side setting:

- Open `http://<printer-ip>/firmware-config/`
- Set **Filament Detection** to **External**

Without that prerequisite, the remote RFID reader cannot update the U1 channel state correctly.

---

## Hardware

### Bill Of Materials

- **ESP32-C3 Super Mini**
- **PN532 NFC/RFID module**
- **4 hookup wires**
  Usually already included with many PN532 boards

### PN532 Mode

Use the PN532 in **HSU mode**.

Important note:

- On the common red PN532 breakout boards, **HSU/UART is usually already the default mode**
- The printed pin labels may still say `SDA` and `SCL`, even though the board is being used in HSU/UART mode

---

## Wiring

### Schematic

<p align="center">
  <img src="./assets/hardware/wiring-schematic.png" alt="ESP32-C3 Super Mini to PN532 wiring" width="760">
</p>

### Pinout List

| ESP32-C3 Super Mini | PN532 board pin | Note |
| --- | --- | --- |
| `3V3` | `VCC` | Power |
| `GND` | `GND` | Ground |
| `GPIO3` | `SCL` | HSU TX line to PN532 board |
| `GPIO4` | `SDA` | HSU RX line from PN532 board |

The firmware release `V1.0` uses:

- `PN532_TX_PIN = 3`
- `PN532_RX_PIN = 4`

---

## Installation

### Web Installer

After publishing GitHub Pages for this repository, the intended installer URL is:

- `https://tinkerbarn.github.io/U1-Argus-Remote-RFID/`

This repository already includes the prepared installer page and manifest:

- [index.html](./index.html)
- [manifest-v1.0.json](./manifest-v1.0.json)

The firmware binary should be placed here:

- [firmware/V1.0](./firmware/V1.0/)

Expected filename for the web installer:

- `U1_Argus_Remote_RFID_V1_0.ino.merged.bin`

### Arduino Source Release

Release source:

- [releases/V1.0/U1_Argus_Remote_RFID_V1_0.ino](./releases/V1.0/U1_Argus_Remote_RFID_V1_0.ino)

Current working sketch:

- [dev/U1_Argus_Remote_RFID_current.ino](./dev/U1_Argus_Remote_RFID_current.ino)

---

## First Setup On The Device

On first boot, the reader opens this hotspot:

- **`U1-Argus-Setup`**

Normally the captive portal starts automatically.

If it does not open by itself:

- open `http://192.168.4.1` in the browser

Then configure the reader like this:

1. Enter the SSID of your home Wi-Fi
2. Enter the Wi-Fi password
3. Enter the IP address of your Snapmaker U1
4. Keep port `7125` unless you intentionally use a different port
5. Enter an mDNS name
   This must be unique if you use multiple readers
6. Choose the channel / tool head the reader belongs to
7. Optionally add already active readers as IP or full URL
   These later appear in the dashboard as quick-jump buttons
8. Save and reboot

After configuration, the dashboard can be opened with:

- `http://example.local`

Replace `example` with the hostname you entered in setup.

---

## Dashboard And Setup Screens

### Dashboard - Channel Status, No Tag Read Yet

<p align="center">
  <img src="./assets/screenshots/dashboard-channel-idle.png" alt="Dashboard showing printer channel state before a valid tag is read" width="900">
</p>

### Dashboard - Tag Read And Channel Updated

<p align="center">
  <img src="./assets/screenshots/dashboard-tag-active.png" alt="Dashboard showing a successfully read tag and updated printer channel" width="900">
</p>

### Setup

<p align="center">
  <img src="./assets/screenshots/setup-screen.png" alt="Setup page for Wi-Fi, printer, hostname, channel, and additional readers" width="670">
</p>

### Captive Portal

<p align="center">
  <img src="./assets/screenshots/captive-portal.jpg" alt="Captive portal start screen on first setup" width="430">
</p>

---

## Release V1.0

`V1.0` is the first public release of this repository.

Highlights:

- ESP32-C3 Super Mini + PN532 in **HSU/UART**
- OpenSpool tag readout over raw PN532 HSU transport
- Snapmaker U1 channel update through external filament-detection workflow
- Captive portal setup with persistent storage
- Live dashboard with printer query, tag state, webhook status, and multi-reader jump links
- English/German support in device UI and web installer

Release source:

- [releases/V1.0/U1_Argus_Remote_RFID_V1_0.ino](./releases/V1.0/U1_Argus_Remote_RFID_V1_0.ino)

Firmware folder:

- [firmware/V1.0](./firmware/V1.0/)

---

## Credits

Many thanks and biggest respect to:

- [paxx12](https://github.com/paxx12)
- [wasikuss](https://github.com/wasikuss)
- [baze](https://gitlab.com/baze)

This project was built with direct inspiration from their work and, in parts, code structure and implementation ideas.

License details and third-party attribution:

- [LICENSE](./LICENSE)
- [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md)
