# U1 Argus Remote RFID Nodes – Persistent Setup and Printer Host Configuration

## Goal

Evaluate and implement persistent storage for all setup-related configuration values used by the U1 Argus Remote RFID node firmware. The configuration should survive firmware updates whenever possible, so users do not need to re-enter the setup data after updating the device.

This design note describes the expected behavior and the configuration items that should be checked and, if necessary, added to persistent storage.

## Background

The U1 Argus Remote RFID project uses ESP32-C3 based remote RFID nodes with PN532 RFID/NFC readers. These nodes communicate over Wi-Fi and interact with a Snapmaker U1 printer or related services.

Currently, setup values may need to be re-entered after a firmware update if they are not stored persistently. This should be reviewed and improved.

## Configuration Values to Persist

Please inspect the existing firmware and setup/dashboard implementation and verify whether the following values are already stored persistently:

- Wi-Fi SSID
- Wi-Fi password
- Printer address
- Selected toolhead
- URLs or IP addresses of the three additional RFID sensor nodes

If one or more of these values are currently only stored in RAM or are reset after a firmware update, update the implementation so that they are stored persistently.

## Persistence Requirements

The configuration should be stored in a way that survives normal firmware updates.

Preferred behavior:

- Configuration is saved after the user changes setup values.
- Configuration is loaded automatically during startup.
- Existing configuration is reused after flashing a newer firmware version, as long as the persistent storage layout is still compatible.
- Reasonable defaults are used when no configuration exists yet.
- Invalid or incomplete configuration values should not crash the firmware.

Use the existing project style and storage mechanism if one already exists. If the project already uses Preferences, NVS, EEPROM emulation, LittleFS, or another persistent storage method, extend that instead of introducing a second unrelated mechanism.

## Printer Address: IP Address or mDNS Name

The printer connection setup should allow the user to enter either:

1. A direct IPv4 address, for example:

   ```text
   192.168.1.120
   ```

2. An mDNS hostname, for example:

   ```text
   u1.local
   ```

The firmware should detect which type of value was configured and use the appropriate connection logic.

## Expected Connection Behavior

When the configured printer address is an IPv4 address:

- Treat the value as a direct IP address.
- Use IP-based connection logic.
- Do not perform mDNS resolution for this value.

When the configured printer address is an mDNS hostname:

- Treat the value as a hostname.
- Resolve it through mDNS or the existing hostname resolution mechanism supported by the network stack.
- Use the resolved address or hostname-based connection logic for the printer connection.

## Address Detection Rules

The implementation should clearly distinguish between IPv4 addresses and hostnames.

Suggested logic:

- If the value matches a valid IPv4 address format, handle it as an IP address.
- Otherwise, handle it as a hostname or mDNS name.
- Hostnames such as `u1.local` should remain valid and must not be rejected simply because they are not numeric IP addresses.

Avoid fragile checks that only look for dots, because both IPv4 addresses and mDNS names contain dots.

## Setup UI Requirements

The setup page or dashboard should make it clear that the printer field accepts either an IP address or an mDNS name.

Suggested label or help text:

```text
Printer address (IP or mDNS, e.g. 192.168.1.120 or u1.local)
```

The same persisted value should be used after reboot and after firmware updates.

## Additional RFID Sensor URLs/IPs

The three additional RFID sensor URL/IP fields should also be reviewed.

For each configured remote sensor endpoint:

- Store the value persistently.
- Reload it during startup.
- Preserve it after firmware updates when possible.
- Keep the current behavior for connection or HTTP communication unless changes are required for persistence.

If these fields may also support hostnames or mDNS names, document and implement the same distinction between IP-based and hostname-based connection logic where relevant.

## Compatibility Notes

When changing the persistent configuration structure, avoid breaking existing installations unnecessarily.

If a versioned configuration format already exists, increment or extend it carefully. If no versioning exists yet, consider adding a small configuration version field so future migrations are easier.

The implementation should not erase existing Wi-Fi credentials or setup values unless the user explicitly performs a reset or the stored data is invalid and cannot be recovered.

## Suggested Implementation Steps

1. Inspect the current setup data model and storage mechanism.
2. Identify whether SSID, Wi-Fi password, printer address, selected toolhead, and the three remote RFID sensor addresses are already persisted.
3. Add missing fields to persistent storage.
4. Add startup loading logic for all setup fields.
5. Add save logic when the user changes setup values.
6. Add helper logic to classify the printer address as either IPv4 or hostname/mDNS.
7. Use IP-based connection logic for IPv4 values.
8. Use hostname or mDNS resolution logic for hostname values such as `u1.local`.
9. Update the setup UI text to explain that both IP and mDNS are supported.
10. Test reboot behavior and firmware update behavior.

## Acceptance Criteria

The task is complete when:

- Wi-Fi SSID and password survive reboot and firmware update.
- Printer address survives reboot and firmware update.
- Selected toolhead survives reboot and firmware update.
- The three additional RFID sensor URLs/IPs survive reboot and firmware update.
- A printer configured by IP address connects using IP-based logic.
- A printer configured by mDNS name, for example `u1.local`, connects using hostname/mDNS logic.
- The setup UI clearly tells the user that both IP and mDNS values are accepted.
- Existing installations are not unnecessarily reset or overwritten.
