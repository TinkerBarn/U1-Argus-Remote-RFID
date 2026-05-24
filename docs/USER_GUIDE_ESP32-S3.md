# User Guide: ESP32-S3 Dual-Reader

Dieser Guide beschreibt die ESP32-S3-N16R8-Version `V1.0` des U1 Argus
Remote RFID Readers. Ein Gerät steuert zwei PN532-Leser für die linke und die
rechte Spulenposition und überträgt erkannte Daten an die zugeordneten
Tool Heads des Snapmaker U1.

## Voraussetzungen

- ESP32-S3 N16R8 mit zwei PN532-Modulen in HSU/UART-Modus
- Installierte ESP32-S3-Firmware `V1.0`
- Snapmaker U1 mit Extended Firmware
- In der Drucker-Firmware ist **Filament Detection** auf **External** gesetzt
- Ein WLAN mit **2,4 GHz**

Der ESP32-S3 unterstützt WLAN nach `802.11 b/g/n` im 2,4-GHz-Band. Er kann
sich nicht mit einem reinen 5-GHz-WLAN verbinden. Das gilt auch dann, wenn
andere Geräte am selben Standort vorzugsweise 5 GHz verwenden.

## Was Die S3-Version Anders Macht

Die S3-Ausgabe führt zwei lokale Reader in einem Controller zusammen:

- **Left spool** und **Right spool** besitzen jeweils einen eigenen PN532 und
  eine eigene Tool-Head-Zuordnung.
- Ein FreeRTOS-Task auf Core 0 bedient die NFC-Leser.
- Die Arduino-Hauptschleife auf Core 1 bedient Webinterface, WLAN und
  Kommunikation mit dem Drucker.
- Die geprüfte schnelle PN532-HSU-Leseroutine unterstützt OpenSpool/NTAG und
  QIDI/MIFARE-Classic-Tags.

Dadurch wird blockierendes NFC-Polling von der Web- und Netzwerkverarbeitung
getrennt. `V1.0` basiert funktional auf dem hardwaregetesteten
Entwicklungsstand `V0.27`.

## Erste Einrichtung

Beim ersten Start oder nach dem Löschen der Einstellungen öffnet das Gerät:

- SSID: `U1-Argus-Setup-XXXX`
- Konfigurationsseite: `http://192.168.4.1`

Verbinden Sie Smartphone oder Computer mit dem Hotspot. Öffnen Sie die
Adresse manuell, falls das Captive Portal nicht automatisch angezeigt wird.

## Setup-Webinterface

| Einstellung | Bedeutung |
| --- | --- |
| Wi-Fi SSID / Password | Zugang zum 2,4-GHz-WLAN mit dem Snapmaker U1 |
| Preferred BSSID 1 / 2 | Optionale Priorisierung bestimmter 2,4-GHz-Access-Points derselben SSID |
| Hostname | Lokaler Name des Readers, beispielsweise `argus-dual`; erreichbar als `http://argus-dual.local` |
| Printer IP / Hostname | IP-Adresse oder mDNS-Hostname des Snapmaker U1 |
| Printer Port | API-Port; standardmäßig `7125` |
| Left spool Tool Head | Druckerkanal für den linken lokalen PN532 |
| Right spool Tool Head | Druckerkanal für den rechten lokalen PN532 |
| Additional dual reader | Optionaler Link zu einem weiteren Zwei-Spulen-Reader mit dessen linken/rechten Tool Heads |
| Serial debug log | Zusätzliche Diagnoseausgaben; kann das Erkennen von Tags verlangsamen und ist standardmäßig aus |
| QIDI config upload / reset | Aktualisiert oder entfernt benutzerdefinierte QIDI-Material- und Herstellerzuordnungen |

Auch bei ausgeschaltetem Debug bleiben wichtige serielle Basisinformationen
wie Setup-Hotspot, WLAN-Status, eine Tag-Erkennung und der Status der
Druckerübertragung verfügbar.

## BSSID-Präferenz

Eine **BSSID** ist die MAC-Adresse einer bestimmten WLAN-Funkzelle, zum
Beispiel `AA:BB:CC:DD:EE:FF`. Sie ist nützlich, wenn mehrere Access Points
oder Mesh-Knoten dieselbe SSID ausstrahlen und der Reader bevorzugt den
nahen oder stabilen 2,4-GHz-Access-Point verwenden soll.

Sind BSSIDs eingetragen, arbeitet die Firmware in dieser Reihenfolge:

1. Sie sucht die sichtbaren WLANs nach der konfigurierten SSID und BSSID 1.
2. Falls BSSID 1 nicht sichtbar ist, wird BSSID 2 versucht.
3. Nur wenn keiner der bevorzugten Access Points sichtbar ist, wird ein
   anderer sichtbarer Access Point derselben SSID verwendet.

Tragen Sie ausschließlich die BSSID des **2,4-GHz**-Netzes ein. Eine BSSID
der 5-GHz-Funkzelle kann der ESP32-S3 nicht verwenden.

### Verfügbare BSSIDs Unter Windows Anzeigen

Öffnen Sie PowerShell oder die Eingabeaufforderung:

```powershell
netsh wlan show networks mode=bssid
```

Eine kompaktere PowerShell-Ansicht:

```powershell
netsh wlan show networks mode=bssid | Select-String 'SSID|BSSID|Signal|Channel'
```

Suchen Sie Ihre SSID und verwenden Sie die BSSID eines 2,4-GHz-Kanals. In der
Regel liegen 2,4-GHz-Kanäle zwischen `1` und `13`.

### Verfügbare BSSIDs Unter macOS Anzeigen

Auf macOS-Versionen, die das ältere `airport`-Systemwerkzeug noch enthalten,
kann ein Scan im Terminal ausgeführt werden:

```sh
/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport -s
```

Apple liefert dieses private Werkzeug nicht auf allen neueren Versionen
weiter aus. Falls der Befehl fehlt, öffnen Sie **Wireless Diagnostics** und
wählen **Window > Scan**, oder zeigen Sie mit gedrückter `Option`-Taste beim
Klick auf das WLAN-Symbol die BSSID der aktuell verbundenen Funkzelle an.

## Unterstützte Tags

- **OpenSpool / NTAG**: Liest OpenSpool-NDEF-Inhalte über den jeweiligen
  Reader und überträgt die Materialdaten an dessen Tool Head.
- **QIDI / MIFARE Classic**: Authentifiziert und liest den QIDI-Datenblock,
  übersetzt Material-, Hersteller- und Farbcodes und aktualisiert den
  betreffenden Kanal.

## QIDI `officiall_filas_list.cfg`

Die Firmware enthält kompakte Standardzuordnungen für gebräuchliche
QIDI-Plus4-Materialien. Über das Setup kann optional die QIDI-Datei
`officiall_filas_list.cfg` hochgeladen werden. Die Schreibweise mit doppeltem
`l` ist der von der Firmware erwartete Dateiname.

Der Upload aktualisiert persistent:

- Materialnummern und deren angezeigte Materialnamen,
- Herstellernummern und deren angezeigte Herstellernamen.

Er überschreibt keine RFID-Tags und installiert nichts auf dem Drucker. Ein
Reset der QIDI-Konfiguration entfernt die hochgeladenen Zuordnungen und
aktiviert wieder die eingebauten Standardwerte.

## Dashboard

Das S3-Dashboard zeigt beide lokalen Spulenpositionen getrennt an.

| Bereich | Angezeigte Informationen |
| --- | --- |
| Statusleiste | WLAN-Verbindung, Drucker-Erreichbarkeit und aktueller Tag-Status |
| Left/Right Printer Tool Head | Zugewiesener Kanal sowie vom Drucker bestätigte Hersteller-, Material-, Farb-, Temperatur-, Sensor- und Official-Daten |
| Left/Right Tag Reader | Zuletzt gelesene UID, Tagquelle (`OpenSpool` oder `QIDI`), Hersteller, Material, Farbe und Temperaturwerte der jeweiligen Spule |
| Set-Ergebnis | Ergebnis der letzten Aktualisierung des jeweiligen Druckerkanals einschließlich HTTP-Status |
| Network | SSID, IP-Adresse, Hostname, Signalstärke, Druckerport und Firmware-Version; die verbundene BSSID wird seriell beim WLAN-Verbindungsaufbau ausgegeben |
| Additional Reader | Schnellzugriff auf ein optional konfiguriertes weiteres Zwei-Spulen-Dashboard |

## Normaler Betrieb

1. Starten Sie Drucker und Reader im selben 2,4-GHz-WLAN.
2. Öffnen Sie das Dashboard über Hostname oder IP-Adresse.
3. Legen Sie eine Spule links oder rechts auf; die Daten werden für den
   zugewiesenen Tool Head gelesen und bei Bedarf an den Drucker gesendet.
4. Nutzen Sie Debug-Logging nur zur Fehlersuche und deaktivieren Sie es für
   die schnellste reguläre Tag-Erkennung.

Bei Verbindungsproblemen prüfen Sie zuerst, ob eine eingetragene bevorzugte
BSSID tatsächlich zu Ihrer 2,4-GHz-SSID gehört und am Aufstellort sichtbar
ist. Prüfen Sie anschließend Drucker-Adresse, Port `7125` und
**Filament Detection: External**.

## Referenzen

- [Espressif ESP32-S3 Produktübersicht](https://www.espressif.com/en/products/socs/esp32-s3/)
- [Espressif ESP32-Chip-Vergleich mit WLAN-Band und CPU-Daten](https://docs.espressif.com/projects/esp-idf/en/v5.0.3/esp32c3/hw-reference/chip-series-comparison.html)
- [Microsoft Dokumentation zu `netsh wlan`](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/netsh-wlan)
