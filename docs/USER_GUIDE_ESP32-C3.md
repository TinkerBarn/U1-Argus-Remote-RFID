# User Guide: ESP32-C3 Single-Reader

Dieser Guide beschreibt die ESP32-C3-Version des U1 Argus Remote RFID Readers.
Sie steuert einen PN532-Leser und überträgt erkannte Filamentdaten per WLAN an
einen frei zugewiesenen Tool Head des Snapmaker U1.

## Voraussetzungen

- ESP32-C3 Super Mini mit einem PN532 in HSU/UART-Modus
- Installierte ESP32-C3-Firmware, aktuell `V2.0`
- Snapmaker U1 mit Extended Firmware
- In der Drucker-Firmware ist **Filament Detection** auf **External** gesetzt
- Ein WLAN mit **2,4 GHz**

Der ESP32-C3 unterstützt WLAN nach `802.11 b/g/n` im 2,4-GHz-Band. Ein
reines 5-GHz-WLAN kann nicht verwendet werden. Bei einem gemeinsamen Namen
für 2,4 und 5 GHz muss der Router weiterhin ein erreichbares 2,4-GHz-Netz
bereitstellen.

## Erste Einrichtung

Beim ersten Start oder nach dem Löschen der Einstellungen öffnet das Gerät
einen Setup-Hotspot:

- SSID: `U1-Argus-Setup-XXXX`
- Konfigurationsseite: `http://192.168.4.1`

Verbinden Sie ein Smartphone oder einen Computer mit diesem Hotspot. Falls das
Captive Portal nicht automatisch erscheint, öffnen Sie die Adresse manuell.

## Setup-Webinterface

| Einstellung | Bedeutung |
| --- | --- |
| Wi-Fi SSID / Password | Zugang zum 2,4-GHz-WLAN, in dem auch der Drucker erreichbar ist |
| Hostname | Name des Readers im lokalen Netz, beispielsweise `argus-left`; erreichbar als `http://argus-left.local` |
| Printer IP / Hostname | IP-Adresse oder mDNS-Hostname des Snapmaker U1 |
| Printer Port | API-Port des Druckers; standardmäßig `7125` |
| Tool Head | Kanal, der mit dem lokalen PN532-Leser aktualisiert wird |
| Additional Readers | Optionale Links zu weiteren separaten Reader-Geräten für die Dashboard-Navigation |
| QIDI config upload | Aktualisiert Material- und Herstellernamen für QIDI-Tags |
| QIDI config reset | Stellt die eingebauten QIDI-Standardzuordnungen wieder her |

Die C3-Version betreibt genau einen lokalen RFID-Leser. Wenn mehrere Spulen
überwacht werden sollen, können mehrere C3-Reader eingerichtet und gegenseitig
im Dashboard verlinkt werden.

## Unterstützte Tags

- **OpenSpool / NTAG**: Liest OpenSpool-NDEF-Daten aus und sendet die
  aufbereiteten Materialinformationen an den zugeordneten Tool Head.
- **QIDI / MIFARE Classic**: Authentifiziert den QIDI-Datenblock, liest
  Material-, Hersteller- und Farbcodes und setzt den Druckerkanal.

## QIDI `officiall_filas_list.cfg`

Die Firmware enthält bereits kompakte Standardzuordnungen für gängige QIDI
Plus4-Materialien. Im Setup kann zusätzlich die QIDI-Konfigurationsdatei
`officiall_filas_list.cfg` hochgeladen werden. Die Schreibweise mit doppeltem
`l` entspricht dem von der Firmware erwarteten Dateinamen.

Der Upload:

- aktualisiert die Zuordnung von QIDI-Materialnummern zu Materialnamen,
- aktualisiert die Zuordnung von Herstellernummern zu Herstellernamen,
- wird persistent im ESP32 gespeichert,
- programmiert keinen RFID-Tag um und verändert keine Drucker-Firmware.

Über **Reset QIDI config** werden hochgeladene Zuordnungen entfernt und die
eingebauten Standardwerte wieder verwendet.

## Dashboard

Das Dashboard zeigt den Betriebszustand des Readers und die zuletzt vom
Drucker bestätigten Filamentdaten.

| Bereich | Angezeigte Informationen |
| --- | --- |
| Statusleiste | WLAN-Verbindung, Drucker-Erreichbarkeit und Tag-Status |
| Printer Tool Head | Zugeordneter Kanal, Hersteller, Materialtyp, Subtyp, Farbe, Hotend-Temperaturen, Bett-Temperatur, Filament-Sensor und Official-Status, soweit vom Drucker geliefert |
| Tag Reader | Letzter gültiger Tag, UID, Quelle (`OpenSpool` oder `QIDI`), gelesener Hersteller, Material, Farbe und Temperaturwerte |
| Set/Webhook-Ergebnis | Ob die Übergabe der Tagdaten an den Drucker erfolgreich war, HTTP-Status und zuletzt gesendete Daten |
| Network | SSID, IP-Adresse, Hostname, Signalstärke, Druckerziel und Firmware-Version |
| Weitere Reader | Schnellzugriff auf konfigurierte zusätzliche Reader-Dashboards |

Ein Tag wird nur dann an den Drucker übertragen, wenn gültige Daten gelesen
wurden und die Aktualisierung für den zugeordneten Kanal erforderlich ist.

## Normaler Betrieb

1. Starten Sie Drucker und Reader im selben 2,4-GHz-WLAN.
2. Öffnen Sie das Dashboard über den konfigurierten Hostnamen oder die
   angezeigte IP-Adresse.
3. Legen Sie eine Spule mit OpenSpool- oder QIDI-Tag an den PN532-Leser.
4. Kontrollieren Sie im Dashboard, ob Tagdaten und Printer Tool Head
   übereinstimmen.

Bei Verbindungsproblemen prüfen Sie zuerst das 2,4-GHz-WLAN, die Drucker-IP
oder den mDNS-Namen, Port `7125` sowie die Druckereinstellung
**Filament Detection: External**.

## Referenz

- [Espressif ESP32-C3 Produktübersicht](https://www.espressif.com/en/products/socs/esp32-c3)
