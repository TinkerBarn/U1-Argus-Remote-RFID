"""Guard the experimental empty-NDEF UID-only lookup behavior."""
from pathlib import Path


source = Path(
    "source/ESP32-S3/V1.8/"
    "U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_8.ino"
).read_text()

assert 'static const char* FW_VERSION = "V1.8";' in source

read_start = source.index("static bool readOpenSpoolFromDetectedTag(")
read_end = source.index("static void processPrinterPolling()", read_start)
reader = source[read_start:read_end]

assert "if (ndefLen == 0)" in reader
assert "useUidOnlyLookup = true;" in reader
assert reader.index("if (!findNdefTlv") < reader.index("if (ndefLen == 0)")
assert reader.index("if (ndefLen == 0)") < reader.index("parseMimeRecord")

poll_start = source.index("static void nfcPollingTask(")
poller = source[poll_start:]
assert 'enqueueNfcTag(reader, "CARD_UID_NDEF0", "", uid, uidLen);' in poller

card_type_start = source.index("static const char* cardTypeForTagSource(")
card_type_end = source.index("static void handleCardUidOnlyTagPayload(", card_type_start)
card_type_mapping = source[card_type_start:card_type_end]
assert "CARD_UID_NDEF0" not in card_type_mapping

payload_start = source.index("static bool buildCardUidOnlyPayload(")
payload_end = source.index("// ------------------------------ Printer API", payload_start)
payload_builder = source[payload_start:payload_end]
assert 'info["CARD_UID"]' in payload_builder
assert 'info["CARD_TYPE"]' in payload_builder
assert "cardType && cardType[0]" in payload_builder

print("PASS: empty NDEF alone selects a payload containing UID without CARD_TYPE")
