# BoxRFID Touch: Snapmaker U1 Printer Send Notes

These notes capture the parts that should be reused when BoxRFID Touch sends
QIDI or OpenSpool tag data directly to a Snapmaker U1 running the paxx12
Extended Firmware.

## Printer Endpoint

Send parsed tag data with a POST request to:

```text
http://<printer-ip-or-hostname>:7125/printer/filament_detect/set
```

Use the assigned U1 channel in the request body:

```json
{
  "channel": 2,
  "info": {
    "VENDOR": "QIDI",
    "MAIN_TYPE": "TPU",
    "RGB_1": 11041023,
    "HOTEND_MIN_TEMP": 200,
    "HOTEND_MAX_TEMP": 250,
    "CARD_UID": [193, 132, 205, 185],
    "CARD_TYPE": "M1"
  }
}
```

U1 channels are zero-based: Tool Head 1 is channel `0`, Tool Head 4 is
channel `3`.

## Required Tag Identity Fields

Always include `CARD_UID` for every successfully detected and parsed tag.

- Format: JSON array of decimal byte values.
- Example UID `C1 84 CD B9` becomes `[193, 132, 205, 185]`.
- Example UID `04 E1 6B BB 45 02 89` becomes `[4, 225, 107, 187, 69, 2, 137]`.

When supported by the printer firmware, also include `CARD_TYPE`:

| Tag family | `CARD_TYPE` |
| --- | --- |
| QIDI / MIFARE Classic | `M1` |
| OpenSpool / NTAG / MIFARE Ultralight | `NTAG` |

This allows future printer-side integrations, including Spoolman-related
logic, to distinguish the physical card family while still receiving the
stable card UID.

## `CARD_TYPE` Compatibility Fallback

Some currently deployed printer firmware builds reject unknown fields and
return an error such as:

```text
unsupported fields: CARD_TYPE
```

On that exact compatibility error:

1. Retry the same `filament_detect/set` payload without `CARD_TYPE`.
2. Remember that `CARD_TYPE` is not accepted for the current runtime session.
3. Continue sending `CARD_UID` on all future requests.

Do not remove `CARD_UID` during fallback.

Pseudo-code:

```cpp
bool printerAcceptsCardType = true;

bool sendTag(JsonObject info) {
  if (!printerAcceptsCardType) {
    info.remove("CARD_TYPE");
  }

  HttpResult result = postFilamentDetectSet(info);
  if (result.httpCode == 400 && result.body.indexOf("unsupported fields: CARD_TYPE") >= 0) {
    printerAcceptsCardType = false;
    info.remove("CARD_TYPE");
    result = postFilamentDetectSet(info);
  }

  return result.ok;
}
```

## Parsed Tag Payload

For known QIDI or OpenSpool tags, send the parsed filament data plus card
identity:

- `VENDOR`
- `MAIN_TYPE`
- `SUB_TYPE` when available
- `RGB_1`
- `HOTEND_MIN_TEMP`
- `HOTEND_MAX_TEMP`
- `BED_TEMP` when available
- `CARD_UID`
- `CARD_TYPE` when supported

Do not send `OFFICIAL`; the printer firmware derives that state.

## UID-Only Fallback Warning

Do not automatically send UID-only data after a temporary parse failure,
authentication failure, or short read glitch.

Current printer firmware can interpret a UID-only request as "tag present but
no filament data", which may clear the channel's material data. UID-only
updates should therefore only be used for a deliberate "unknown tag present"
workflow and only with firmware behavior that explicitly supports it.

If such a workflow is intentionally enabled, the minimal payload is:

```json
{
  "channel": 2,
  "info": {
    "CARD_UID": [193, 132, 205, 185],
    "CARD_TYPE": "M1"
  }
}
```

Apply the same `CARD_TYPE` compatibility fallback described above.

## Safety Before Sending

Before changing filament metadata, check the assigned tool-head filament
sensor:

```text
filament_motion_sensor e<channel>_filament.filament_detected
```

When `filament_detected` is `true`, do not send new tag data. This avoids
changing filament metadata after filament has already reached the tool head.

Moonraker WebSocket subscriptions are preferred for observing printer state
efficiently. The actual `filament_detect/set` operation can remain a REST POST.

## Verification

After a successful set operation, query or observe the printer's channel data
and compare it with the desired payload. Retry only when the printer state does
not match and the filament sensor still reports no loaded filament.

For future Spoolman compatibility, store or compare UIDs carefully:

- The printer endpoint expects `CARD_UID` as a decimal byte array.
- Spoolman-style metadata commonly stores the same UID as uppercase hex
  without separators, for example `C184CDB9`.
