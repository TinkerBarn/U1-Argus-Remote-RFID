"""Guard persistence, setup copy and fast UID-only dispatch in test 2."""
from pathlib import Path


source = Path(
    "source/ESP32-S3/V1.8/"
    "U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_8.ino"
).read_text()

assert "bool openSpoolUidOnly;" in source
assert "char source[20]" in source
assert 'prefs.getBool("openspool_uid", false)' in source
assert 'prefs.putBool("openspool_uid", gSettings.openSpoolUidOnly)' in source
assert 'web.hasArg("openspool_uid_only")' in source
assert "gSettings.openSpoolUidOnly = openSpoolUidOnly;" in source

setup_start = source.index("static String configPageHtml(const String& msg) {")
setup_end = source.index("static void handleDebugLogApi()", setup_start)
setup = source[setup_start:setup_end]
for expected in (
    "OpenSpool UID-only Mode",
    "only the UID of every detected tag is sent",
    "No filament information stored on the tag is read or transmitted",
    "paxx12 firmware with the Spoolman option enabled",
    "A matching Spoolman profile must exist for every UID",
):
    assert expected in setup

poll_start = source.index("static void nfcPollingTask(")
poll_end = source.index("// ============================== Setup / Loop", poll_start)
poller = source[poll_start:poll_end]
mode_pos = poller.index("if (gSettings.openSpoolUidOnly)")
qidi_pos = poller.index("if (uidLen == 4)")
assert mode_pos < qidi_pos
uid_mode = poller[mode_pos:qidi_pos]
assert 'enqueueNfcTag(reader, "CARD_UID_ONLY", "", uid, uidLen);' in uid_mode
assert "readQidiFromCurrentTag" not in uid_mode
assert "readOpenSpoolFromDetectedTag" not in uid_mode

mapping_start = source.index("static const char* cardTypeForTagSource(")
mapping_end = source.index("static void handleCardUidOnlyTagPayload(", mapping_start)
assert "CARD_UID_ONLY" not in source[mapping_start:mapping_end]

assert 'enqueueNfcTag(reader, "CARD_UID_NDEF0", "", uid, uidLen);' in poller
assert 'enqueueNfcTag(reader, "OpenSpool", openspoolJson, uid, uidLen);' in poller
assert 'enqueueNfcTag(reader, "QIDI", qidiJson, uid, uidLen);' in poller

print("PASS: UID-only mode is persistent, documented, fast, and optional")
