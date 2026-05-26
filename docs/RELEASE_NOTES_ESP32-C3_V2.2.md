# ESP32-C3 Single-Reader V2.2

ESP32-C3 Single-Reader V2.2 adds the filament-loaded safety lock while
preserving the compact single-reader feature set.

## Changes

- Before sending tag data to the printer, the firmware performs a fresh
  filament motion-sensor query for the assigned Tool Head.
- A tag-triggered `filament_detect/set` is sent only when the Tool Head reports
  `filament_detected=false`.
- Once filament is detected, PN532 polling is paused to prevent an accidental
  tag read from replacing the active filament metadata.
- While filament remains loaded, printer motion polling is reduced to
  approximately one query every five seconds.
- SET retries use the same filament-state protection.
- The single-core main loop continues servicing printer polling after a valid
  tag read, allowing a newly loaded sensor state to activate the lock reliably.

## Retained Baseline

- Retains the V2.1 PN532, QIDI, and OpenSpool tag-reading paths unchanged.
- Retains preferred BSSID selection and connected-BSSID dashboard display.
- Keeps the added firmware footprint intentionally small for the nearly full
  ESP32-C3 application partition.
