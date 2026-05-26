/*
  U1 Argus Remote RFID - ESP32-S3 N16R8 V1.2

  Target: ESP32-S3 N16R8 + 2x PN532 (HSU/UART)

  Required libraries:
  - Adafruit PN532
  - ArduinoJson

  Wiring for ESP32-S3 N16R8 -> PN532 in HSU/UART mode:
  - 3V3        -> both PN532 VCC
  - GND        -> both PN532 GND
  Alternative pin test:
  - GPIO11 TX  -> left PN532 pin labeled SCL
  - GPIO12 RX  -> left PN532 pin labeled SDA
  - GPIO13 TX  -> right PN532 pin labeled SCL
  - GPIO14 RX  -> right PN532 pin labeled SDA
*/

#include <WiFi.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_PN532.h>
#include <HWCDC.h>
#include <soc/soc_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#ifndef ARDUINO_USB_MODE
#define ARDUINO_USB_MODE 0
#endif
#ifndef ARDUINO_USB_CDC_ON_BOOT
#define ARDUINO_USB_CDC_ON_BOOT 0
#endif

// Mirror Serial logs to the ESP32-S3 USB-Serial/JTAG port when the Arduino
// board profile maps Serial to UART0 or native USB CDC instead.
#if SOC_USB_SERIAL_JTAG_SUPPORTED && !(ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT)
static HWCDC DebugUsbJtagSerial;
#define U1A_HAS_USB_JTAG_LOG 1
#else
#define U1A_HAS_USB_JTAG_LOG 0
#endif

class SerialMirrorLogger {
public:
  void begin(unsigned long baud) {
    ::Serial.begin(baud);
#if U1A_HAS_USB_JTAG_LOG
    DebugUsbJtagSerial.begin(baud);
#endif
  }

  size_t print(const char* s) {
    size_t written = ::Serial.print(s);
#if U1A_HAS_USB_JTAG_LOG
    DebugUsbJtagSerial.print(s);
#endif
    return written;
  }

  size_t print(char c) {
    size_t written = ::Serial.print(c);
#if U1A_HAS_USB_JTAG_LOG
    DebugUsbJtagSerial.print(c);
#endif
    return written;
  }

  size_t print(const String& s) {
    size_t written = ::Serial.print(s);
#if U1A_HAS_USB_JTAG_LOG
    DebugUsbJtagSerial.print(s);
#endif
    return written;
  }

  size_t println(const char* s) {
    size_t written = ::Serial.println(s);
#if U1A_HAS_USB_JTAG_LOG
    DebugUsbJtagSerial.println(s);
#endif
    return written;
  }

  size_t println(const String& s) {
    size_t written = ::Serial.println(s);
#if U1A_HAS_USB_JTAG_LOG
    DebugUsbJtagSerial.println(s);
#endif
    return written;
  }

  size_t println() {
    size_t written = ::Serial.println();
#if U1A_HAS_USB_JTAG_LOG
    DebugUsbJtagSerial.println();
#endif
    return written;
  }

  int printf(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len < 0) return len;
    print(buf);
    return len;
  }

  void flush() {
    ::Serial.flush();
#if U1A_HAS_USB_JTAG_LOG
    DebugUsbJtagSerial.flush();
#endif
  }
};

static SerialMirrorLogger DebugSerial;
#define Serial DebugSerial

// ============================== Version ==============================
static const char* FW_NAME = "U1 Argus Remote RFID";
static const char* FW_VERSION = "V1.2";
static const char* BOARD_LABEL = "ESP32-S3 N16R8";

// ============================== Debug ==============================
static const uint32_t SERIAL_BAUD = 115200;
static bool gDebugSerialEnabled = false;
// This S3 board has an addressable RGB LED on GPIO48, not a simple status LED.
static const int TAG_LED_PIN = -1;
static const bool TAG_LED_ACTIVE_HIGH = true;
static const uint32_t TAG_LED_HOLD_MS = 300;
static const uint32_t TAG_ERROR_BLINK_MS = 120;
#define NFC_DIAG_SERIAL (isDebugSerialEnabled())
static const uint32_t NFC_NO_TAG_LOG_MS = 5000;

// ============================== Pins ==============================
// Wiring for ESP32-S3 N16R8 -> PN532 in HSU/UART mode:
// - 3V3        -> both PN532 VCC
// - GND        -> both PN532 GND
// Alternative pin test:
// - GPIO11 TX  -> left PN532 pin labeled SCL
// - GPIO12 RX  -> left PN532 pin labeled SDA
// - GPIO13 TX  -> right PN532 pin labeled SCL
// - GPIO14 RX  -> right PN532 pin labeled SDA
//
// IMPORTANT:
// - The PN532 board must be switched to HSU/UART mode.
// - Some PN532 boards keep the printed SCL/SDA labels even when the mode
//   switches are set to HSU/UART. We therefore follow the known-good wiring
//   from your other project exactly.
// - Reset is optional and is not wired in the initial 4-wire setup.
static const uint8_t READER_COUNT = 2;
static const char* READER_LABELS[READER_COUNT] = {"Left spool", "Right spool"};
static const int PN532_RX_PINS[READER_COUNT] = {12, 14};
static const int PN532_TX_PINS[READER_COUNT] = {11, 13};
static const int PN532_RESET_PINS[READER_COUNT] = {-1, -1};
static const uint32_t PN532_BAUD = 115200;

// ============================== Timing ==============================
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
static const uint32_t WIFI_PORTAL_FALLBACK_MS = 30000;
static const uint32_t WIFI_RECOVERY_RETRY_MS = 15000;
static const uint32_t WIFI_RECOVERY_ATTEMPT_MS = 6000;
static const uint32_t WIFI_STARTUP_STAGGER_MAX_MS = 2500;

// --- NFC polling intervals ---
// BENCH:  used at startup / when feeder has never triggered.  Both readers alternate.
// IDLE:   feeder known but currently inactive.  Both readers alternate (slower).
// ACTIVE: one reader has focus-lock or feeder is pulling filament.  Single reader, fast.
static const uint32_t TAG_BENCH_POLL_MS = 30;     // V0.7: was 45 – faster dual-scan startup
static const uint32_t TAG_IDLE_POLL_MS = 900;
static const uint32_t TAG_ACTIVE_POLL_MS = 12;
static const uint32_t TAG_ACTIVE_WINDOW_MS = 1200;

// Short focus while a detected tag is being handled. V0.18 releases this focus
// immediately after each completed read attempt so re-reading is not blocked.
static const uint32_t TAG_DETECT_FOCUS_MS = 600;

static const uint32_t TAG_SAME_UID_SKIP_MS = 0;
static const uint32_t FEEDER_PRIORITY_WINDOW_MS = 12000;
static const uint32_t PRINTER_MOTION_QUERY_MS = 600;
static const uint32_t PRINTER_LOADED_MOTION_QUERY_MS = 5000;
static const uint32_t PRINTER_INFO_SYNC_MS = 90000;
static const uint32_t PRINTER_TAG_INFO_MAX_AGE_MS = 1000;
static const uint32_t PRINTER_QUERY_JITTER_MS = 450;
static const uint8_t PRINTER_QUERY_STAGGER_SLOTS = 4;
static const uint16_t PRINTER_QUERY_TIMEOUT_MS = 700;
static const uint32_t WEBHOOK_VERIFY_DELAY_MS = 1200;
static const uint32_t WEBHOOK_VERIFY_RETRY_MS = 1800;
static const uint8_t WEBHOOK_VERIFY_MAX_RETRIES = 0;
static const uint32_t WEBHOOK_RESEND_GRACE_MS = 6000;
static const uint16_t PN532_CMD_TIMEOUT_MS = 100;
static const uint16_t PN532_ACK_TIMEOUT_MS = 10;
// Detect timeouts: how long InListPassiveTarget blocks waiting for a tag.
// SCAN  (dual-reader, no focus) – keep short so we rotate between readers quickly.
// ACTIVE (single focused reader) – can be longer; we know a tag is likely there.
static const uint16_t PN532_TAG_DETECT_TIMEOUT_BENCH_MS = 60;
static const uint16_t PN532_TAG_DETECT_TIMEOUT_IDLE_MS  = 60;
static const uint16_t PN532_TAG_DETECT_TIMEOUT_ACTIVE_MS = 60;
static const uint16_t PN532_MIFARE_TIMEOUT_MS = 80;
static const uint16_t PN532_FAST_READ_TIMEOUT_MS = 120;
static const bool PN532_USE_LIBRARY_TAG_DETECT = false;
static const bool PN532_SWITCH_RF_FIELD_BETWEEN_READERS = false;
static const uint8_t PN532_RF_SETTLE_MS = 8;
static const uint8_t PN532_POST_READ_RF_RESET_MS = 4;
static const uint8_t PN532_PASSIVE_RETRIES = 0x10;
static const uint8_t TAG_READ_SETTLE_MS = 2;
static const uint8_t NTAG_FIRST_USER_PAGE = 4;
static const uint8_t NTAG_LAST_USER_PAGE = 225;
static const size_t NTAG_USER_BYTES = (NTAG_LAST_USER_PAGE - NTAG_FIRST_USER_PAGE + 1) * 4;
static const uint8_t NTAG_CC_PAGE = 3;
static const uint8_t NTAG_INITIAL_FAST_READ_PAGES = 16;
static const uint8_t NTAG_FAST_READ_PAGES = 48;

// ============================== QIDI UID cache ==============================
// Disabled by default: reprogrammed QIDI tags can keep the same UID while material,
// vendor, and color bytes change.
static const uint8_t QIDI_UID_CACHE_SIZE = 4; // enough for 2 readers with one spare each
static const bool QIDI_UID_CACHE_ENABLED = false;
static const uint32_t QIDI_PRESENT_CACHE_MS = 1500;
static const uint32_t QIDI_AUTH_FAIL_COOLDOWN_MS = 250;
static const uint8_t QIDI_AUTH_SETTLE_MS = 0;
static const uint8_t NFC_QUEUE_DEPTH = 4;
static const size_t NFC_QUEUE_JSON_CAP = 2048;
static const uint32_t NFC_TASK_STACK_BYTES = 12288;

// ============================== Network defaults ==============================
static const char* AP_SSID_BASE = "U1-Argus-Setup";
static const char* AP_PASS = ""; // open AP as requested
static const uint16_t WEB_PORT = 80;
static const byte DNS_PORT = 53;
static const uint16_t DEFAULT_PRINTER_PORT = 7125;
static const uint8_t REMOTE_READER_COUNT = 1;
static const uint8_t WIFI_BSSID_COUNT = 2;
static const uint8_t WIFI_VISIBLE_BSSID_MAX = 32;
static const uint32_t WIFI_VISIBLE_BSSID_REFRESH_MS = 300000;
static const uint8_t PREF_CONFIG_VERSION = 2;
static const uint32_t PRINTER_MDNS_CACHE_MS = 120000;
static const uint32_t PRINTER_MDNS_RETRY_MS = 15000;
static const uint32_t PRINTER_MDNS_QUERY_TIMEOUT_MS = 800;
static const uint8_t QIDI_DATA_BLOCK = 4;
static const uint8_t QIDI_MAX_MATERIALS = 50;
static const uint8_t QIDI_MAX_VENDORS = 16;
static const size_t QIDI_CFG_UPLOAD_MAX_BYTES = 32000;

// ============================== Storage ==============================
Preferences prefs;
static const char* PREF_NS = "u1argus";
static const char* PREF_QIDI_NS = "u1qidi";

struct QidiMaterialRecord {
  uint8_t id;
  char type[18];
  uint16_t minTemp;
  uint16_t maxTemp;
} __attribute__((packed));

struct QidiVendorRecord {
  uint8_t id;
  char name[18];
} __attribute__((packed));

static const QidiMaterialRecord DEFAULT_QIDI_MATERIALS[] = {
  {1, "PLA", 190, 240},
  {2, "PLA", 190, 240},
  {3, "PLA", 190, 240},
  {4, "PLA", 190, 240},
  {5, "PLA-CF", 210, 250},
  {6, "PLA", 190, 240},
  {7, "PLA", 190, 240},
  {8, "PLA", 190, 230},
  {11, "ABS", 240, 280},
  {12, "ABS-GF", 240, 280},
  {13, "ABS", 240, 280},
  {14, "ABS", 240, 280},
  {18, "ASA", 240, 280},
  {19, "ASA-Aero", 240, 280},
  {24, "UltraPA", 260, 300},
  {25, "PA12-CF", 260, 300},
  {26, "UltraPA-CF25", 300, 320},
  {30, "PAHT-CF", 300, 320},
  {31, "PAHT-GF", 300, 320},
  {32, "PAHT-S", 260, 280},
  {33, "PA-S", 260, 280},
  {34, "PC-ABS-FR", 260, 280},
  {37, "PET-CF", 280, 320},
  {38, "PET-GF", 280, 320},
  {39, "PETG", 240, 280},
  {40, "PETG", 240, 275},
  {41, "PETG", 220, 270},
  {44, "PPS-CF", 300, 350},
  {45, "PETG", 240, 280},
  {47, "PVA", 210, 250},
  {49, "TPU-AERO", 200, 250},
  {50, "TPU", 200, 250}
};

static const QidiVendorRecord DEFAULT_QIDI_VENDORS[] = {
  {0, "Generic"},
  {1, "QIDI"}
};

static const uint32_t QIDI_COLOR_RGB[] = {
  0x000000, 0xFAFAFA, 0x060606, 0xD9E3ED, 0x5CF30F, 0x63E492,
  0x2850FF, 0xFE98FE, 0xDFD628, 0x228332, 0x99DEFF, 0x1714B0,
  0xCEC0FE, 0xCADE4B, 0x1353AB, 0x5EA9FD, 0xA878FF, 0xFE717A,
  0xFF362D, 0xE2DFCD, 0x898F9B, 0x6E3812, 0xCAC59F, 0xF28636,
  0xB87F2B
};

QidiMaterialRecord gQidiMaterials[QIDI_MAX_MATERIALS];
QidiVendorRecord gQidiVendors[QIDI_MAX_VENDORS];
uint8_t gQidiMaterialCount = 0;
uint8_t gQidiVendorCount = 0;
bool gQidiCustomConfig = false;
bool gQidiLastSaveOk = true;
uint16_t gQidiCfgBytes = 0;

struct Settings {
  char wifiSsid[33];
  char wifiPass[65];
  char wifiBssids[WIFI_BSSID_COUNT][18];
  char hostname[33];
  char printerIp[64];
  uint16_t printerPort;
  uint8_t channels[READER_COUNT];
  char remoteReaders[REMOTE_READER_COUNT][96];
  uint8_t remoteReaderTools[REMOTE_READER_COUNT][READER_COUNT];
  bool debugSerial;
};

Settings gSettings = {
  "", "", {"", ""}, "u1-argus-rfid", "192.168.1.10", DEFAULT_PRINTER_PORT, {0, 1}, {""}, {{0, 0}}, false
};

// ============================== Web ==============================
WebServer web(WEB_PORT);
DNSServer dnsServer;
bool portalMode = false;
bool portalRoutesReady = false;
bool mdnsRunning = false;
char apSsid[32] = "";
String cachedPrinterAddress = "";
String cachedPrinterHost = "";
uint32_t cachedPrinterResolveMs = 0;
bool cachedPrinterResolveOk = false;
String qidiCfgUploadBuffer = "";
bool qidiCfgUploadTooLarge = false;

struct VisibleWifiBssidState {
  String bssid;
  int32_t rssi;
  int32_t channel;
  bool connected;
};

VisibleWifiBssidState gVisibleWifiBssids[WIFI_VISIBLE_BSSID_MAX];
uint8_t gVisibleWifiBssidCount = 0;
uint32_t lastVisibleWifiBssidScanMs = 0;
String visibleWifiBssidStateFingerprint;
bool visibleWifiScanSerialPending = false;

// ============================== PN532 ==============================
HardwareSerial PN532SerialLeft(1);
HardwareSerial PN532SerialRight(2);
HardwareSerial* PN532Serials[READER_COUNT] = {&PN532SerialLeft, &PN532SerialRight};
Adafruit_PN532 nfcLeft(PN532_RESET_PINS[0], &PN532SerialLeft);
Adafruit_PN532 nfcRight(PN532_RESET_PINS[1], &PN532SerialRight);
Adafruit_PN532* nfcs[READER_COUNT] = {&nfcLeft, &nfcRight};
bool nfcReady[READER_COUNT] = {false, false};
uint8_t activeReaderIndex = 0;
uint8_t activeNfcReaderIndex = 0;
#define PN532Serial (*PN532Serials[activeNfcReaderIndex])
#define nfc (*nfcs[activeNfcReaderIndex])

// ============================== Runtime state ==============================
struct TagState {
  bool hasData = false;
  bool active = false;
  uint32_t lastSeenMs = 0;
  String fingerprint;
  String uidHex;
  String vendor;
  String mainType;
  String subType;
  String colorHex;
  String source;
  int minTemp = -1;
  int maxTemp = -1;
  int bedTemp = -1;
  String openspoolJson;
  String mappedPayload;
};

struct OpenSpoolFields {
  String vendor;
  String mainType;
  String subType;
  String colorHex;
  int minTemp = -1;
  int maxTemp = -1;
  int bedTemp = -1;
  int alpha = -1;
  bool officialKnown = false;
  bool official = false;
};

struct PrinterChannelState {
  bool wifiConnected = false;
  bool queryOk = false;
  bool hasInfo = false;
  bool motionKnown = false;
  bool filamentDetected = false;
  bool officialKnown = false;
  bool official = false;
  int httpCode = 0;
  uint32_t lastQueryMs = 0;
  String endpoint;
  String error;
  String fingerprint;
  String vendor;
  String manufacturer;
  String mainType;
  String subType;
  String colorHex;
  String cardUidCsv;
  int minTemp = -1;
  int maxTemp = -1;
  int bedTemp = -1;
  String rawJson;
};

struct WebhookState {
  bool known = false;
  bool ok = false;
  int httpCode = 0;
  uint32_t lastSentMs = 0;
  String response;
};

struct NfcQueueItem {
  uint8_t reader = 0;
  uint8_t uid[10] = {0};
  uint8_t uidLen = 0;
  char source[12] = {0};
  char json[NFC_QUEUE_JSON_CAP] = {0};
};

QueueHandle_t nfcTagQueue = nullptr;
TaskHandle_t nfcTaskHandle = nullptr;
uint32_t lastPollMs[READER_COUNT] = {0, 0};
uint8_t lastAcceptedUid[READER_COUNT][10] = {{0}};
uint8_t lastAcceptedUidLen[READER_COUNT] = {0, 0};
uint32_t lastAcceptedUidMs[READER_COUNT] = {0, 0};
uint8_t lastQidiAuthFailUid[READER_COUNT][4] = {{0}};
bool lastQidiAuthFailKnown[READER_COUNT] = {false, false};
uint32_t lastQidiAuthFailMs[READER_COUNT] = {0, 0};

// ============================== QIDI UID → JSON cache ==============================
// Kept as an optional implementation detail, but disabled for normal builds because
// rewritten QIDI cards can retain the same UID.
struct QidiUidCacheEntry {
  uint8_t  uid[4];
  bool     valid;
  char     json[256]; // max expected QIDI JSON is well under 200 chars
};
static QidiUidCacheEntry qidiUidCache[QIDI_UID_CACHE_SIZE] = {};

struct QidiPresenceCacheEntry {
  uint8_t uid[4];
  bool valid;
  uint32_t lastSeenMs;
  char json[256];
};
static QidiPresenceCacheEntry qidiPresenceCache[READER_COUNT] = {};

static bool qidiCacheLookup(const uint8_t* uid, String& outJson) {
  if (!QIDI_UID_CACHE_ENABLED) return false;
  for (uint8_t i = 0; i < QIDI_UID_CACHE_SIZE; i++) {
    if (!qidiUidCache[i].valid) continue;
    if (memcmp(qidiUidCache[i].uid, uid, 4) == 0) {
      outJson = String(qidiUidCache[i].json);
      return true;
    }
  }
  return false;
}

static void qidiCacheStore(const uint8_t* uid, const String& json) {
  if (!QIDI_UID_CACHE_ENABLED) return;
  // Find an empty slot; if none, evict the first (oldest) entry (simple FIFO).
  int8_t slot = -1;
  for (uint8_t i = 0; i < QIDI_UID_CACHE_SIZE; i++) {
    if (!qidiUidCache[i].valid) { slot = (int8_t)i; break; }
  }
  if (slot < 0) {
    // No free slot: rotate – shift entries down, free slot 0
    for (uint8_t i = 0; i < QIDI_UID_CACHE_SIZE - 1; i++) qidiUidCache[i] = qidiUidCache[i + 1];
    slot = QIDI_UID_CACHE_SIZE - 1;
    qidiUidCache[slot].valid = false;
  }
  memcpy(qidiUidCache[slot].uid, uid, 4);
  size_t n = json.length();
  if (n >= sizeof(qidiUidCache[slot].json)) n = sizeof(qidiUidCache[slot].json) - 1;
  memcpy(qidiUidCache[slot].json, json.c_str(), n);
  qidiUidCache[slot].json[n] = '\0';
  qidiUidCache[slot].valid = true;
}

static bool qidiPresenceCacheLookup(uint8_t reader, const uint8_t* uid, String& outJson) {
  if (reader >= READER_COUNT || !uid) return false;
  QidiPresenceCacheEntry& entry = qidiPresenceCache[reader];
  if (!entry.valid) return false;
  if (memcmp(entry.uid, uid, 4) != 0) return false;
  uint32_t now = millis();
  if ((now - entry.lastSeenMs) > QIDI_PRESENT_CACHE_MS) {
    entry.valid = false;
    return false;
  }
  entry.lastSeenMs = now;
  outJson = String(entry.json);
  return true;
}

static void qidiPresenceCacheStore(uint8_t reader, const uint8_t* uid, const String& json) {
  if (reader >= READER_COUNT || !uid) return;
  QidiPresenceCacheEntry& entry = qidiPresenceCache[reader];
  memcpy(entry.uid, uid, 4);
  size_t n = json.length();
  if (n >= sizeof(entry.json)) n = sizeof(entry.json) - 1;
  memcpy(entry.json, json.c_str(), n);
  entry.json[n] = '\0';
  entry.lastSeenMs = millis();
  entry.valid = true;
}

static void qidiPresenceCacheNoteNoTag(uint8_t reader) {
  if (reader >= READER_COUNT) return;
  QidiPresenceCacheEntry& entry = qidiPresenceCache[reader];
  if (!entry.valid) return;
  if ((millis() - entry.lastSeenMs) > QIDI_PRESENT_CACHE_MS) {
    entry.valid = false;
  }
}
uint32_t lastNfcProbeLogMs[READER_COUNT] = {0, 0};
uint32_t lastTagSeenMs[READER_COUNT] = {0, 0};
uint32_t tagLedUntilMs = 0;
uint32_t nextMotionQueryMs[READER_COUNT] = {0, 0};
uint32_t nextInfoSyncMs[READER_COUNT] = {0, 0};
uint32_t lastPrinterInfoQueryMs[READER_COUNT] = {0, 0};
uint32_t lastFeederMotionRiseMs[READER_COUNT] = {0, 0};
bool feederMotionBaselineKnown[READER_COUNT] = {false, false};
bool lastFeederMotionValue[READER_COUNT] = {false, false};
uint32_t nfcFocusUntilMs = 0;
int8_t nfcFocusReader = -1;
uint32_t pendingVerifyDueMs[READER_COUNT] = {0, 0};
uint32_t wifiLostSinceMs = 0;
uint32_t lastWifiReconnectMs = 0;
uint32_t lastWifiRecoveryMs = 0;
uint32_t stateRevision = 1;
String lastSentFingerprint[READER_COUNT];
String lastObservedFingerprint[READER_COUNT];
String pendingVerifyPayload[READER_COUNT];
String pendingVerifyFingerprint[READER_COUNT];
String pendingVerifyExpectedWithUid[READER_COUNT];
String pendingVerifyExpectedNoUid[READER_COUNT];
uint8_t pendingVerifyRetriesLeft[READER_COUNT] = {0, 0};
TagState gTagStates[READER_COUNT];
PrinterChannelState gPrinterStates[READER_COUNT];
WebhookState gWebhookStates[READER_COUNT];
#define gTagState (gTagStates[activeReaderIndex])
#define gPrinterState (gPrinterStates[activeReaderIndex])
#define gWebhookState (gWebhookStates[activeReaderIndex])

// ------------------------------ Helpers ------------------------------
static String configPageHtml(const String& msg = "");
static String dashboardPageHtml();
static bool tryParseIntField(JsonVariantConst v, int& out);
static String normalizedReaderUrl(const char* raw);
static String configuredMdnsName();
static bool findNdefTlv(const uint8_t* buf, size_t len, size_t& ndefOffset, size_t& ndefLen);
static String currentPrinterComparableFingerprint();
static bool isUnsignedInteger(const String& value);
static void loadQidiConfig();
static bool importQidiCfgText(const String& cfgText, uint8_t& materialCount, uint8_t& vendorCount);
static void handleQidiCfgUpload();
static void handleQidiCfgUploadChunk();
static void handleQidiCfgReset();
static void sendHtmlNoCache(int code, const String& body);
static void selectReader(uint8_t readerIndex);
static bool isDebugSerialEnabled();

static void selectReader(uint8_t readerIndex) {
  activeReaderIndex = readerIndex < READER_COUNT ? readerIndex : 0;
}

static void selectNfcReader(uint8_t readerIndex) {
  activeNfcReaderIndex = readerIndex < READER_COUNT ? readerIndex : 0;
}

static bool isDebugSerialEnabled() {
  return gDebugSerialEnabled;
}

static void debugPrintImpl(const String& msg) {
  Serial.println(msg);
}

static void debugPrintfImpl(const char* fmt, ...) {
  char buf[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
}

#define debugPrint(msg) do { if (isDebugSerialEnabled()) debugPrintImpl(String(msg)); } while (0)
#define debugPrintf(...) do { if (isDebugSerialEnabled()) debugPrintfImpl(__VA_ARGS__); } while (0)

static String bytesToHexString(const uint8_t* data, uint8_t len) {
  String out;
  for (uint8_t i = 0; i < len; i++) {
    if (i) out += ' ';
    if (data[i] < 0x10) out += '0';
    out += String(data[i], HEX);
  }
  out.toUpperCase();
  return out;
}

static String uidBytesToCsv(const uint8_t* data, uint8_t len) {
  String out;
  for (uint8_t i = 0; i < len; i++) {
    if (i) out += ',';
    out += String((int)data[i]);
  }
  return out;
}

static String uidJsonArrayToCsv(JsonVariantConst v) {
  JsonArrayConst arr = v.as<JsonArrayConst>();
  if (arr.isNull()) return "";

  String out;
  bool first = true;
  for (JsonVariantConst item : arr) {
    int value = -1;
    if (item.is<int>()) value = item.as<int>();
    else if (item.is<long>()) value = (int)item.as<long>();
    else if (item.is<const char*>()) value = String(item.as<const char*>()).toInt();
    if (value < 0 || value > 255) continue;

    if (!first) out += ',';
    out += String(value);
    first = false;
  }
  return out;
}

static void setTagLed(bool on) {
  if (TAG_LED_PIN < 0) return;
  digitalWrite(TAG_LED_PIN, on == TAG_LED_ACTIVE_HIGH ? HIGH : LOW);
}

static void pulseTagLed() {
  setTagLed(true);
  tagLedUntilMs = millis() + TAG_LED_HOLD_MS;
}

static void pulseTagLedError() {
  setTagLed(true);
  tagLedUntilMs = millis() + TAG_ERROR_BLINK_MS;
}

static void bumpStateRevision() {
  stateRevision++;
  if (stateRevision == 0) stateRevision = 1;
}

static String rgbIntToHexString(int rgb) {
  if (rgb < 0 || rgb > 0xFFFFFF) return "";
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02X%02X%02X", (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
  return String(buf);
}

static void flushPn532Input() {
  while (PN532Serial.available()) {
    (void)PN532Serial.read();
  }
}

static int16_t receivePn532Bytes(uint8_t* buf, size_t len, uint16_t timeoutMs) {
  size_t readBytes = 0;
  while (readBytes < len) {
    const uint32_t start = millis();
    int ret = -1;
    do {
      ret = PN532Serial.read();
      if (ret >= 0) break;
    } while ((timeoutMs == 0) || ((millis() - start) < timeoutMs));

    if (ret < 0) {
      if (readBytes > 0) return (int16_t)readBytes;
      return -2;
    }
    buf[readBytes++] = (uint8_t)ret;
  }
  return (int16_t)readBytes;
}

static bool pn532SendFrame(uint8_t command, const uint8_t* payload, size_t payloadLen) {
  if (payloadLen > 252) return false;

  const uint8_t dataLen = (uint8_t)(payloadLen + 2);
  const uint8_t lcs = (uint8_t)(~dataLen + 1);
  uint8_t dcs = (uint8_t)(0xD4 + command);
  for (size_t i = 0; i < payloadLen; ++i) {
    dcs = (uint8_t)(dcs + payload[i]);
  }
  dcs = (uint8_t)(~dcs + 1);

  PN532Serial.write((uint8_t)0x00);
  PN532Serial.write((uint8_t)0x00);
  PN532Serial.write((uint8_t)0xFF);
  PN532Serial.write(dataLen);
  PN532Serial.write(lcs);
  PN532Serial.write((uint8_t)0xD4);
  PN532Serial.write(command);
  for (size_t i = 0; i < payloadLen; ++i) {
    PN532Serial.write(payload[i]);
  }
  PN532Serial.write(dcs);
  PN532Serial.write((uint8_t)0x00);
  PN532Serial.flush();
  return true;
}

static bool pn532ReadAck(uint16_t timeoutMs) {
  uint8_t ack[6] = {0};
  static const uint8_t expected[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  int16_t ackRead = receivePn532Bytes(ack, sizeof(ack), timeoutMs);
  if (ackRead <= 0) return false;
  for (size_t i = 0; i < sizeof(expected); ++i) {
    if (ack[i] != expected[i]) return false;
  }
  return true;
}

static bool pn532ReadResponseFrame(uint8_t expectedResponseCode,
                                   uint8_t* dataOut,
                                   size_t dataOutCap,
                                   size_t& dataLenOut,
                                   uint16_t timeoutMs) {
  uint8_t preamble[3] = {0};
  if (receivePn532Bytes(preamble, sizeof(preamble), timeoutMs) <= 0) return false;
  if (preamble[0] != 0x00 || preamble[1] != 0x00 || preamble[2] != 0xFF) return false;

  uint8_t length[2] = {0};
  if (receivePn532Bytes(length, sizeof(length), timeoutMs) <= 0) return false;
  uint8_t len = length[0];
  const uint8_t lcs = length[1];
  if ((uint8_t)(len + lcs) != 0x00) return false;
  if (len < 2) return false;
  len = (uint8_t)(len - 2);
  if (len > dataOutCap) return false;

  uint8_t cmd[2] = {0};
  if (receivePn532Bytes(cmd, sizeof(cmd), timeoutMs) <= 0) return false;
  if (cmd[0] != 0xD5 || cmd[1] != expectedResponseCode) return false;

  if (receivePn532Bytes(dataOut, len, timeoutMs) != (int16_t)len) return false;

  uint8_t checksum[2] = {0};
  if (receivePn532Bytes(checksum, sizeof(checksum), timeoutMs) <= 0) return false;
  uint8_t sum = (uint8_t)(0xD5 + expectedResponseCode);
  for (uint8_t i = 0; i < len; ++i) {
    sum = (uint8_t)(sum + dataOut[i]);
  }
  if ((uint8_t)(sum + checksum[0]) != 0x00 || checksum[1] != 0x00) return false;

  dataLenOut = len;
  return true;
}

static bool pn532Transact(uint8_t command,
                          const uint8_t* payload,
                          size_t payloadLen,
                          uint8_t expectedResponseCode,
                          uint8_t* respData,
                          size_t respCap,
                          size_t& respLen,
                          uint16_t timeoutMs) {
  flushPn532Input();
  if (!pn532SendFrame(command, payload, payloadLen)) return false;
  if (!pn532ReadAck(PN532_ACK_TIMEOUT_MS)) return false;
  return pn532ReadResponseFrame(expectedResponseCode, respData, respCap, respLen, timeoutMs);
}

static bool pn532SetRfField(bool enabled) {
  const uint8_t payload[] = {
    0x01, // RFConfiguration item: RF field
    (uint8_t)(enabled ? 0x01 : 0x00)
  };
  uint8_t resp[8] = {0};
  size_t respLen = 0;
  return pn532Transact(0x32, payload, sizeof(payload), 0x33, resp, sizeof(resp), respLen, PN532_CMD_TIMEOUT_MS);
}

static bool pn532ReleaseTargets() {
  const uint8_t payload[] = {0x00}; // release all targets
  uint8_t resp[8] = {0};
  size_t respLen = 0;
  return pn532Transact(0x52, payload, sizeof(payload), 0x53, resp, sizeof(resp), respLen, PN532_CMD_TIMEOUT_MS);
}

static void pn532ResetAfterTagRead(const char* reason) {
  bool releaseOk = pn532ReleaseTargets();
  if (releaseOk) return;

  bool offOk = pn532SetRfField(false);
  delay(PN532_POST_READ_RF_RESET_MS);
  bool onOk = pn532SetRfField(true);
  if (NFC_DIAG_SERIAL) {
    Serial.printf("[PN532 %s] post-read reset reason=%s release=failed rf_off=%s rf_on=%s\n",
                  READER_LABELS[activeNfcReaderIndex],
                  reason ? reason : "tag",
                  offOk ? "ok" : "failed",
                  onOk ? "ok" : "failed");
  }
}

static void prepareReaderRfField(uint8_t readerIndex) {
  if (!PN532_SWITCH_RF_FIELD_BETWEEN_READERS) return;

  static int8_t activeRfReader = -1;
  if (activeRfReader == (int8_t)readerIndex) return;

  for (uint8_t i = 0; i < READER_COUNT; i++) {
    if (i == readerIndex || !nfcReady[i]) continue;
    uint8_t previous = activeNfcReaderIndex;
    selectNfcReader(i);
    bool offOk = pn532SetRfField(false);
    if (NFC_DIAG_SERIAL && !offOk) {
      Serial.printf("[PN532 %s] RF field off failed\n", READER_LABELS[i]);
    }
    selectNfcReader(previous);
  }

  if (nfcReady[readerIndex]) {
    uint8_t previous = activeNfcReaderIndex;
    selectNfcReader(readerIndex);
    bool onOk = pn532SetRfField(true);
    if (NFC_DIAG_SERIAL && !onOk) {
      Serial.printf("[PN532 %s] RF field on failed\n", READER_LABELS[readerIndex]);
    }
    selectNfcReader(previous);
    delay(PN532_RF_SETTLE_MS);
  }

  activeRfReader = readerIndex;
}

static bool pn532InListPassiveTarget(uint8_t* uidOut, uint8_t& uidLenOut, uint16_t timeoutMs) {
  if (PN532_USE_LIBRARY_TAG_DETECT) {
    uint8_t libUidLen = 0;
    bool ok = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uidOut, &libUidLen, timeoutMs);
    if (!ok || libUidLen == 0 || libUidLen > 10) return false;
    uidLenOut = libUidLen;
    return true;
  }

  const uint8_t payload[] = {0x01, 0x00};
  uint8_t resp[64] = {0};
  size_t respLen = 0;
  if (!pn532Transact(0x4A, payload, sizeof(payload), 0x4B, resp, sizeof(resp), respLen, timeoutMs)) {
    return false;
  }
  if (respLen < 7 || resp[0] != 1) return false;
  const uint8_t uidLen = resp[5];
  if (uidLen == 0 || uidLen > 10 || respLen < (size_t)(6 + uidLen)) return false;
  uidLenOut = uidLen;
  for (uint8_t i = 0; i < uidLen; ++i) {
    uidOut[i] = resp[6 + i];
  }
  return true;
}

static bool pn532ReadPageWindow(uint8_t startPage, uint8_t* data16) {
  const uint8_t payload[] = {0x01, 0x30, startPage};
  uint8_t resp[32] = {0};
  size_t respLen = 0;
  if (!pn532Transact(0x40, payload, sizeof(payload), 0x41, resp, sizeof(resp), respLen, PN532_CMD_TIMEOUT_MS)) {
    return false;
  }
  if (respLen < 17 || resp[0] != 0x00) return false;
  memcpy(data16, resp + 1, 16);
  return true;
}

static bool pn532FastReadPages(uint8_t startPage,
                               uint8_t endPage,
                               uint8_t* dataOut,
                               size_t dataOutCap,
                               uint8_t& pageCountOut) {
  if (!dataOut || endPage < startPage) return false;
  uint8_t pageCount = (uint8_t)(endPage - startPage + 1);
  size_t dataLen = (size_t)pageCount * 4u;
  if (dataLen > dataOutCap) return false;

  const uint8_t payload[] = {0x01, 0x3A, startPage, endPage};
  uint8_t resp[1 + (NTAG_FAST_READ_PAGES * 4)] = {0};
  size_t respLen = 0;
  if (!pn532Transact(0x40, payload, sizeof(payload), 0x41, resp, sizeof(resp), respLen, PN532_FAST_READ_TIMEOUT_MS)) {
    return false;
  }
  if (respLen < (1 + dataLen) || resp[0] != 0x00) return false;

  memcpy(dataOut, resp + 1, dataLen);
  pageCountOut = pageCount;
  return true;
}

static bool pn532MifareClassicAuthBlock(const uint8_t* uid, uint8_t uidLen, uint8_t block) {
  if (!uid || uidLen != 4) return false;
  uint8_t payload[13] = {
    0x01, // target number
    0x60, // MIFARE auth with Key A
    block,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    uid[0], uid[1], uid[2], uid[3]
  };
  uint8_t resp[8] = {0};
  size_t respLen = 0;
  if (!pn532Transact(0x40, payload, sizeof(payload), 0x41, resp, sizeof(resp), respLen, PN532_MIFARE_TIMEOUT_MS)) {
    return false;
  }
  return respLen >= 1 && resp[0] == 0x00;
}

static bool reselectCurrentTagUid(const uint8_t* uid, uint8_t uidLen, uint16_t timeoutMs);

static bool qidiAuthBlockOnce(const uint8_t* uid, uint8_t uidLen, uint8_t block) {
  if (QIDI_AUTH_SETTLE_MS > 0) delay(QIDI_AUTH_SETTLE_MS);
  return pn532MifareClassicAuthBlock(uid, uidLen, block);
}

static bool pn532MifareClassicReadBlock(uint8_t block, uint8_t* data16) {
  const uint8_t payload[] = {0x01, 0x30, block};
  uint8_t resp[24] = {0};
  size_t respLen = 0;
  if (!pn532Transact(0x40, payload, sizeof(payload), 0x41, resp, sizeof(resp), respLen, PN532_MIFARE_TIMEOUT_MS)) {
    return false;
  }
  if (respLen < 17 || resp[0] != 0x00) return false;
  memcpy(data16, resp + 1, 16);
  return true;
}

static bool ntagLastUserPageFromCc(const uint8_t* cc, uint16_t& lastPageOut) {
  if (cc[0] != 0xE1) {
    debugPrintf("[DEBUG] warning: unexpected CC magic 0x%02X\n", cc[0]);
  }
  const uint16_t dataBytes = (uint16_t)cc[2] * 8u;
  if (dataBytes < 4) return false;
  const uint16_t pageCount = dataBytes / 4u;
  lastPageOut = (uint16_t)(NTAG_FIRST_USER_PAGE + pageCount - 1u);
  if (lastPageOut > NTAG_LAST_USER_PAGE) lastPageOut = NTAG_LAST_USER_PAGE;
  return true;
}

static bool pn532GetLastUserPage(uint16_t& lastPageOut, uint8_t* ccWindowOut) {
  uint8_t cc[16] = {0};
  if (!pn532ReadPageWindow(NTAG_CC_PAGE, cc)) {
    debugPrint("[DEBUG] failed to read CC page via raw HSU");
    return false;
  }
  if (ccWindowOut) memcpy(ccWindowOut, cc, 16);
  return ntagLastUserPageFromCc(cc, lastPageOut);
}

static void copyNtagPagesToUserBuffer(uint16_t startPage,
                                      const uint8_t* data,
                                      uint8_t pageCount,
                                      uint16_t lastPage,
                                      uint8_t* out,
                                      size_t& actualLen) {
  for (uint8_t offset = 0; offset < pageCount; ++offset) {
    const uint16_t thisPage = (uint16_t)(startPage + offset);
    if (thisPage < NTAG_FIRST_USER_PAGE) continue;
    if (thisPage > lastPage) break;

    const size_t dst = (size_t)(thisPage - NTAG_FIRST_USER_PAGE) * 4u;
    out[dst + 0] = data[offset * 4 + 0];
    out[dst + 1] = data[offset * 4 + 1];
    out[dst + 2] = data[offset * 4 + 2];
    out[dst + 3] = data[offset * 4 + 3];
    actualLen = dst + 4;
  }
}

static bool hasCompleteNdefInBuffer(const uint8_t* buf, size_t len, uint16_t lastReadPage) {
  size_t ndefOffset = 0, ndefLen = 0;
  if (!findNdefTlv(buf, len, ndefOffset, ndefLen)) return false;

  const size_t needed = ndefOffset + ndefLen + 1;
  debugPrintf("[DEBUG] NDEF candidate found at offset=%u len=%u after page=%u\n",
              (unsigned)ndefOffset,
              (unsigned)ndefLen,
              (unsigned)lastReadPage);
  if (len < needed) return false;

  debugPrintf("[DEBUG] Completed raw HSU read at page=%u total=%u bytes\n",
              (unsigned)lastReadPage,
              (unsigned)len);
  return true;
}

static void safeCopy(char* dst, size_t dstSize, const String& src) {
  if (!dst || dstSize == 0) return;
  size_t n = src.length();
  if (n >= dstSize) n = dstSize - 1;
  memcpy(dst, src.c_str(), n);
  dst[n] = '\0';
}

static String cfgTrimmed(String s) {
  s.trim();
  if (s.startsWith("\"") && s.endsWith("\"") && s.length() >= 2) {
    s = s.substring(1, s.length() - 1);
    s.trim();
  }
  return s;
}

static int extractTrailingNumber(const String& text) {
  int start = -1;
  for (int i = 0; i < (int)text.length(); i++) {
    if (isDigit((unsigned char)text[i])) {
      if (start < 0) start = i;
    } else if (start >= 0) {
      return text.substring(start, i).toInt();
    }
  }
  if (start >= 0) return text.substring(start).toInt();
  return -1;
}

static uint16_t parseCfgU16(const String& text) {
  String s = text;
  s.trim();
  if (!s.length()) return 0;
  long value = s.toInt();
  if (value < 0) value = 0;
  if (value > 65535L) value = 65535L;
  return (uint16_t)value;
}

static void qidiUpsertMaterial(QidiMaterialRecord* records, uint8_t& count, const QidiMaterialRecord& rec) {
  if (rec.id == 0 || rec.id > QIDI_MAX_MATERIALS || rec.type[0] == '\0') return;
  for (uint8_t i = 0; i < count; i++) {
    if (records[i].id == rec.id) {
      records[i] = rec;
      return;
    }
  }
  if (count < QIDI_MAX_MATERIALS) {
    records[count++] = rec;
  }
}

static void qidiUpsertVendor(QidiVendorRecord* records, uint8_t& count, const QidiVendorRecord& rec) {
  if (rec.id >= QIDI_MAX_VENDORS || rec.name[0] == '\0') return;
  for (uint8_t i = 0; i < count; i++) {
    if (records[i].id == rec.id) {
      records[i] = rec;
      return;
    }
  }
  if (count < QIDI_MAX_VENDORS) {
    records[count++] = rec;
  }
}

static bool saveQidiConfig() {
  String blob;
  blob.reserve(1600);
  blob += "Q1\n";
  for (uint8_t i = 0; i < gQidiMaterialCount; i++) {
    String type = String(gQidiMaterials[i].type);
    type.replace("|", " ");
    type.replace("\n", " ");
    type.replace("\r", " ");
    blob += "M|";
    blob += String(gQidiMaterials[i].id);
    blob += "|";
    blob += type;
    blob += "|";
    blob += String(gQidiMaterials[i].minTemp);
    blob += "|";
    blob += String(gQidiMaterials[i].maxTemp);
    blob += "\n";
  }
  for (uint8_t i = 0; i < gQidiVendorCount; i++) {
    String name = String(gQidiVendors[i].name);
    name.replace("|", " ");
    name.replace("\n", " ");
    name.replace("\r", " ");
    blob += "V|";
    blob += String(gQidiVendors[i].id);
    blob += "|";
    blob += name;
    blob += "\n";
  }

  prefs.begin(PREF_QIDI_NS, false);
  prefs.clear();
  size_t written = prefs.putString("cfg", blob);
  prefs.end();
  bool ok = (written == blob.length() || written == blob.length() + 1);
  gQidiCustomConfig = (gQidiMaterialCount > 0 || gQidiVendorCount > 0);
  gQidiLastSaveOk = ok;
  gQidiCfgBytes = (uint16_t)blob.length();
  if (isDebugSerialEnabled() || !ok) {
    Serial.printf("[QIDI] cfg save %s: materials=%u vendors=%u bytes=%u written=%u\n",
                  ok ? "ok" : "failed",
                  (unsigned)gQidiMaterialCount,
                  (unsigned)gQidiVendorCount,
                  (unsigned)blob.length(),
                  (unsigned)written);
  }
  return ok;
}

static void loadQidiConfig() {
  memset(gQidiMaterials, 0, sizeof(gQidiMaterials));
  memset(gQidiVendors, 0, sizeof(gQidiVendors));
  gQidiMaterialCount = 0;
  gQidiVendorCount = 0;
  gQidiCfgBytes = 0;

  prefs.begin(PREF_QIDI_NS, true);
  String blob = prefs.getString("cfg", "");
  prefs.end();
  gQidiCfgBytes = (uint16_t)blob.length();

  size_t pos = 0;
  while (pos <= blob.length()) {
    size_t next = blob.indexOf('\n', pos);
    String line = (next == (size_t)-1) ? blob.substring(pos) : blob.substring(pos, next);
    pos = (next == (size_t)-1) ? blob.length() + 1 : next + 1;
    line.trim();
    if (!line.length() || line == "Q1") continue;

    int p1 = line.indexOf('|');
    int p2 = line.indexOf('|', p1 + 1);
    if (p1 <= 0 || p2 <= p1) continue;
    String kind = line.substring(0, p1);

    if (kind == "M") {
      int p3 = line.indexOf('|', p2 + 1);
      int p4 = line.indexOf('|', p3 + 1);
      if (p3 <= p2 || p4 <= p3 || gQidiMaterialCount >= QIDI_MAX_MATERIALS) continue;
      int id = line.substring(p1 + 1, p2).toInt();
      if (id <= 0 || id > QIDI_MAX_MATERIALS) continue;
      QidiMaterialRecord rec = {};
      rec.id = (uint8_t)id;
      safeCopy(rec.type, sizeof(rec.type), line.substring(p2 + 1, p3));
      rec.minTemp = parseCfgU16(line.substring(p3 + 1, p4));
      rec.maxTemp = parseCfgU16(line.substring(p4 + 1));
      qidiUpsertMaterial(gQidiMaterials, gQidiMaterialCount, rec);
    } else if (kind == "V") {
      if (gQidiVendorCount >= QIDI_MAX_VENDORS) continue;
      int id = line.substring(p1 + 1, p2).toInt();
      if (id < 0 || id >= QIDI_MAX_VENDORS) continue;
      QidiVendorRecord rec = {};
      rec.id = (uint8_t)id;
      safeCopy(rec.name, sizeof(rec.name), line.substring(p2 + 1));
      qidiUpsertVendor(gQidiVendors, gQidiVendorCount, rec);
    }
  }

  gQidiCustomConfig = (gQidiMaterialCount > 0 || gQidiVendorCount > 0);
  gQidiLastSaveOk = true;
  if (isDebugSerialEnabled()) {
    Serial.printf("[QIDI] cfg loaded: %s, materials=%u, vendors=%u, bytes=%u\n",
                  gQidiCustomConfig ? "custom" : "built-in",
                  (unsigned)gQidiMaterialCount,
                  (unsigned)gQidiVendorCount,
                  (unsigned)gQidiCfgBytes);
  }
}

static const QidiMaterialRecord* findQidiMaterial(uint8_t id) {
  for (uint8_t i = 0; i < gQidiMaterialCount; i++) {
    if (gQidiMaterials[i].id == id) return &gQidiMaterials[i];
  }
  for (uint8_t i = 0; i < (uint8_t)(sizeof(DEFAULT_QIDI_MATERIALS) / sizeof(DEFAULT_QIDI_MATERIALS[0])); i++) {
    if (DEFAULT_QIDI_MATERIALS[i].id == id) return &DEFAULT_QIDI_MATERIALS[i];
  }
  return nullptr;
}

static const char* qidiVendorName(uint8_t id) {
  for (uint8_t i = 0; i < gQidiVendorCount; i++) {
    if (gQidiVendors[i].id == id) return gQidiVendors[i].name;
  }
  for (uint8_t i = 0; i < (uint8_t)(sizeof(DEFAULT_QIDI_VENDORS) / sizeof(DEFAULT_QIDI_VENDORS[0])); i++) {
    if (DEFAULT_QIDI_VENDORS[i].id == id) return DEFAULT_QIDI_VENDORS[i].name;
  }
  return "QIDI";
}

static int qidiColorRgb(uint8_t colorId) {
  if (colorId == 0 || colorId >= (uint8_t)(sizeof(QIDI_COLOR_RGB) / sizeof(QIDI_COLOR_RGB[0]))) return -1;
  return (int)QIDI_COLOR_RGB[colorId];
}

static bool importQidiCfgText(const String& cfgText, uint8_t& materialCount, uint8_t& vendorCount) {
  QidiMaterialRecord materials[QIDI_MAX_MATERIALS] = {};
  QidiVendorRecord vendors[QIDI_MAX_VENDORS] = {};
  materialCount = 0;
  vendorCount = 0;

  String section;
  QidiMaterialRecord cur = {};
  bool inFila = false;

  auto commitFila = [&]() {
    if (!inFila || cur.id == 0) return;
    if (cur.type[0] == '\0') return;
    qidiUpsertMaterial(materials, materialCount, cur);
  };

  size_t pos = 0;
  while (pos <= cfgText.length()) {
    size_t next = cfgText.indexOf('\n', pos);
    String rawLine = (next == (size_t)-1) ? cfgText.substring(pos) : cfgText.substring(pos, next);
    pos = (next == (size_t)-1) ? cfgText.length() + 1 : next + 1;

    int semi = rawLine.indexOf(';');
    int hash = rawLine.indexOf('#');
    int comment = -1;
    if (semi >= 0 && hash >= 0) comment = min(semi, hash);
    else if (semi >= 0) comment = semi;
    else if (hash >= 0) comment = hash;
    if (comment >= 0) rawLine.remove(comment);

    String line = cfgTrimmed(rawLine);
    if (!line.length()) continue;

    if (line.startsWith("[") && line.endsWith("]")) {
      commitFila();
      section = cfgTrimmed(line.substring(1, line.length() - 1));
      section.toLowerCase();
      memset(&cur, 0, sizeof(cur));
      inFila = false;
      if (section.startsWith("fila")) {
        int id = extractTrailingNumber(section);
        if (id > 0 && id <= QIDI_MAX_MATERIALS) {
          cur.id = (uint8_t)id;
          inFila = true;
        }
      }
      continue;
    }

    int eq = line.indexOf('=');
    if (eq < 0) continue;
    String key = cfgTrimmed(line.substring(0, eq));
    String value = cfgTrimmed(line.substring(eq + 1));
    String lowerKey = key;
    lowerKey.toLowerCase();

    if (section == "vendor_list") {
      int id = isUnsignedInteger(lowerKey) ? lowerKey.toInt() : extractTrailingNumber(lowerKey);
      if (id >= 0 && id < QIDI_MAX_VENDORS && value.length()) {
        QidiVendorRecord rec = {};
        rec.id = (uint8_t)id;
        safeCopy(rec.name, sizeof(rec.name), value);
        qidiUpsertVendor(vendors, vendorCount, rec);
      }
      continue;
    }

    if (inFila) {
      if (lowerKey == "filament" && cur.type[0] == '\0') safeCopy(cur.type, sizeof(cur.type), value);
      else if (lowerKey == "type") safeCopy(cur.type, sizeof(cur.type), value);
      else if (lowerKey == "min_temp") cur.minTemp = parseCfgU16(value);
      else if (lowerKey == "max_temp") cur.maxTemp = parseCfgU16(value);
    }
  }
  commitFila();

  if (materialCount == 0 && vendorCount == 0) return false;
  memset(gQidiMaterials, 0, sizeof(gQidiMaterials));
  memset(gQidiVendors, 0, sizeof(gQidiVendors));
  memcpy(gQidiMaterials, materials, (size_t)materialCount * sizeof(QidiMaterialRecord));
  memcpy(gQidiVendors, vendors, (size_t)vendorCount * sizeof(QidiVendorRecord));
  gQidiMaterialCount = materialCount;
  gQidiVendorCount = vendorCount;
  bool saved = saveQidiConfig();
  return saved;
}

static void resetQidiConfig() {
  prefs.begin(PREF_QIDI_NS, false);
  prefs.clear();
  prefs.end();
  loadQidiConfig();
  Serial.println("[QIDI] cfg reset to built-in defaults");
}

static String htmlEscape(const String& in) {
  String out = in;
  out.replace("&", "&amp;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  return out;
}

static void sendHtmlNoCache(int code, const String& body) {
  web.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  web.sendHeader("Pragma", "no-cache");
  web.sendHeader("Expires", "0");
  web.send(code, "text/html; charset=utf-8", body);
}

static String normalizedReaderUrl(const char* raw) {
  String url = raw ? String(raw) : "";
  url.trim();
  if (!url.length()) return "";
  if (!url.startsWith("http://") && !url.startsWith("https://")) {
    url = "http://" + url;
  }
  return url;
}

static bool isDecDigit(char c) {
  return c >= '0' && c <= '9';
}

static bool isAlphaNumChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || isDecDigit(c);
}

static bool isUnsignedInteger(const String& value) {
  if (!value.length()) return false;
  for (uint16_t i = 0; i < value.length(); i++) {
    if (!isDecDigit(value[i])) return false;
  }
  return true;
}

static String normalizedHostAddress(const String& raw) {
  String value = raw;
  value.trim();
  String lower = value;
  lower.toLowerCase();
  if (lower.startsWith("http://")) {
    value = value.substring(7);
  } else if (lower.startsWith("https://")) {
    value = value.substring(8);
  }

  int slash = value.indexOf('/');
  if (slash >= 0) value = value.substring(0, slash);

  int colon = value.lastIndexOf(':');
  if (colon > 0) {
    String maybePort = value.substring(colon + 1);
    if (isUnsignedInteger(maybePort)) value = value.substring(0, colon);
  }

  value.trim();
  return value;
}

static bool isIpv4Address(const String& address) {
  IPAddress tmp;
  return tmp.fromString(address);
}

static bool isValidHostname(const String& host) {
  if (!host.length() || host.length() > 63) return false;
  if (host[0] == '.' || host[host.length() - 1] == '.') return false;

  bool hasNameChar = false;
  uint8_t labelLen = 0;
  char last = 0;
  for (uint16_t i = 0; i < host.length(); i++) {
    char c = host[i];
    if (c == '.') {
      if (labelLen == 0 || last == '-') return false;
      labelLen = 0;
      last = c;
      continue;
    }
    if (!isAlphaNumChar(c) && c != '-') return false;
    if (labelLen == 0 && c == '-') return false;
    if (!isDecDigit(c)) hasNameChar = true;
    labelLen++;
    if (labelLen > 63) return false;
    last = c;
  }
  if (labelLen == 0 || last == '-') return false;
  return hasNameChar;
}

static bool isMdnsName(const String& host) {
  String lower = host;
  lower.toLowerCase();
  return lower.endsWith(".local");
}

static String mdnsQueryName(const String& host) {
  String query = host;
  String lower = query;
  lower.toLowerCase();
  if (lower.endsWith(".local")) query = query.substring(0, query.length() - 6);
  return query;
}

static bool isValidPrinterAddress(const String& address) {
  if (!address.length()) return false;
  if (isIpv4Address(address)) return true;
  return isValidHostname(address);
}

static const char* printerAddressType(const String& address) {
  if (isIpv4Address(address)) return "ip";
  if (isMdnsName(address)) return "mdns";
  return "hostname";
}

static int8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return (int8_t)(c - '0');
  if (c >= 'a' && c <= 'f') return (int8_t)(10 + c - 'a');
  if (c >= 'A' && c <= 'F') return (int8_t)(10 + c - 'A');
  return -1;
}

static bool parseBssid(const char* value, uint8_t* bytesOut) {
  if (!value || strlen(value) != 17 || !bytesOut) return false;
  for (uint8_t i = 0; i < 6; i++) {
    const uint8_t offset = (uint8_t)(i * 3);
    int8_t high = hexNibble(value[offset]);
    int8_t low = hexNibble(value[offset + 1]);
    if (high < 0 || low < 0) return false;
    bytesOut[i] = (uint8_t)((high << 4) | low);
    if (i < 5 && value[offset + 2] != ':' && value[offset + 2] != '-') return false;
  }
  return true;
}

static bool normalizeBssid(const String& raw, char* valueOut, size_t valueOutSize) {
  if (!valueOut || valueOutSize < 18) return false;
  String value = raw;
  value.trim();
  if (!value.length()) {
    valueOut[0] = '\0';
    return true;
  }

  uint8_t bytes[6] = {0};
  if (!parseBssid(value.c_str(), bytes)) return false;
  snprintf(valueOut, valueOutSize, "%02X:%02X:%02X:%02X:%02X:%02X",
           bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
  return true;
}

static bool parseIpPort(const char* address, uint16_t port) {
  if (port == 0) return false;
  String clean = normalizedHostAddress(address ? String(address) : "");
  return isValidPrinterAddress(clean);
}

static String resolvedPrinterHostForUrl() {
  String address = normalizedHostAddress(gSettings.printerIp);
  if (!address.length() || isIpv4Address(address) || !isMdnsName(address) || WiFi.status() != WL_CONNECTED) {
    return address;
  }

  uint32_t now = millis();
  if (cachedPrinterAddress == address) {
    uint32_t age = now - cachedPrinterResolveMs;
    if (cachedPrinterResolveOk && age < PRINTER_MDNS_CACHE_MS) return cachedPrinterHost;
    if (!cachedPrinterResolveOk && age < PRINTER_MDNS_RETRY_MS) return address;
  }

  IPAddress resolved = MDNS.queryHost(mdnsQueryName(address), PRINTER_MDNS_QUERY_TIMEOUT_MS);
  cachedPrinterAddress = address;
  cachedPrinterResolveMs = now;
  cachedPrinterResolveOk = ((uint32_t)resolved != 0);
  cachedPrinterHost = cachedPrinterResolveOk ? resolved.toString() : address;

  if (cachedPrinterResolveOk) {
    Serial.printf("[MDNS] Printer %s resolved to %s\n", address.c_str(), cachedPrinterHost.c_str());
  } else {
    debugPrintf("[DEBUG] Printer mDNS resolve failed for %s, falling back to hostname URL\n", address.c_str());
  }
  return cachedPrinterHost;
}

static String printerBaseUrl() {
  return String("http://") + resolvedPrinterHostForUrl() + ":" + String(gSettings.printerPort);
}

static String configuredMdnsName() {
  String host = String(gSettings.hostname);
  host.trim();
  if (!host.length()) host = "u1-argus-rfid";
  String lower = host;
  lower.toLowerCase();
  if (!lower.endsWith(".local")) host += ".local";
  return host;
}

static bool isCaptiveRequestHost(const String& host) {
  if (!host.length()) return false;
  IPAddress ip;
  if (ip.fromString(host)) return false;
  String lower = host;
  lower.toLowerCase();
  if (lower.endsWith(".local")) return false;
  return true;
}

static bool shouldRedirectToPortal() {
  if (!portalMode) return false;
  return isCaptiveRequestHost(web.hostHeader());
}

static void sendPortalRedirect() {
  IPAddress apIp = WiFi.softAPIP();
  String location = String("http://") + apIp.toString() + "/";
  web.sendHeader("Location", location, true);
  web.send(302, "text/plain", "");
}

static void sendPortalLanding() {
  if (shouldRedirectToPortal()) {
    sendPortalRedirect();
  } else {
    sendHtmlNoCache(200, portalMode ? configPageHtml() : dashboardPageHtml());
  }
}

// ------------------------------ Settings ------------------------------
static void loadSettings() {
  prefs.begin(PREF_NS, true);
  String ssid = prefs.getString("wifi_ssid", "");
  String pass = prefs.getString("wifi_pass", "");
  String bssids[WIFI_BSSID_COUNT] = {
    prefs.getString("wifi_bssid_1", ""),
    prefs.getString("wifi_bssid_2", "")
  };
  String host = prefs.getString("hostname", "u1-argus-rfid");
  String ip = normalizedHostAddress(prefs.getString("printer_ip", "192.168.1.10"));
  uint16_t port = prefs.getUShort("printer_port", DEFAULT_PRINTER_PORT);
  uint8_t legacyChannel = prefs.getUChar("channel", 0);
  uint8_t leftChannel = prefs.getUChar("channel_left", legacyChannel);
  uint8_t rightChannel = prefs.getUChar("channel_right", legacyChannel < 3 ? legacyChannel + 1 : 1);
  bool debugSerial = prefs.getBool("debug_serial", false);
  if (port == 0) port = DEFAULT_PRINTER_PORT;
  if (!isValidPrinterAddress(ip)) ip = "192.168.1.10";

  safeCopy(gSettings.wifiSsid, sizeof(gSettings.wifiSsid), ssid);
  safeCopy(gSettings.wifiPass, sizeof(gSettings.wifiPass), pass);
  for (uint8_t i = 0; i < WIFI_BSSID_COUNT; i++) {
    if (!normalizeBssid(bssids[i], gSettings.wifiBssids[i], sizeof(gSettings.wifiBssids[i]))) {
      gSettings.wifiBssids[i][0] = '\0';
    }
  }
  safeCopy(gSettings.hostname, sizeof(gSettings.hostname), host);
  safeCopy(gSettings.printerIp, sizeof(gSettings.printerIp), ip);
  gSettings.printerPort = port;
  gSettings.channels[0] = (leftChannel > 3) ? 0 : leftChannel;
  gSettings.channels[1] = (rightChannel > 3) ? 1 : rightChannel;
  gSettings.debugSerial = debugSerial;
  gDebugSerialEnabled = debugSerial;
  for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
    String key = String("reader_") + String(i + 2);
    String val = prefs.getString(key.c_str(), "");
    safeCopy(gSettings.remoteReaders[i], sizeof(gSettings.remoteReaders[i]), val);

    String legacyToolKey = String("reader_tool_") + String(i + 2);
    uint8_t legacyTool = prefs.getUChar(legacyToolKey.c_str(), 0);
    String leftToolKey = String("rtool_") + String(i + 2) + "_l";
    String rightToolKey = String("rtool_") + String(i + 2) + "_r";
    uint8_t leftTool = prefs.getUChar(leftToolKey.c_str(), legacyTool);
    uint8_t rightTool = prefs.getUChar(rightToolKey.c_str(), 0);
    gSettings.remoteReaderTools[i][0] = (leftTool >= 1 && leftTool <= 4) ? leftTool : 0;
    gSettings.remoteReaderTools[i][1] = (rightTool >= 1 && rightTool <= 4) ? rightTool : 0;
  }
  prefs.end();
}

static void saveSettings() {
  prefs.begin(PREF_NS, false);
  prefs.putUChar("cfg_ver", PREF_CONFIG_VERSION);
  prefs.putString("wifi_ssid", gSettings.wifiSsid);
  prefs.putString("wifi_pass", gSettings.wifiPass);
  prefs.putString("wifi_bssid_1", gSettings.wifiBssids[0]);
  prefs.putString("wifi_bssid_2", gSettings.wifiBssids[1]);
  prefs.putString("hostname", gSettings.hostname);
  prefs.putString("printer_ip", gSettings.printerIp);
  prefs.putUShort("printer_port", gSettings.printerPort);
  prefs.putUChar("channel", gSettings.channels[0]); // legacy compatibility for older single-reader builds
  prefs.putUChar("channel_left", gSettings.channels[0]);
  prefs.putUChar("channel_right", gSettings.channels[1]);
  prefs.putBool("debug_serial", gSettings.debugSerial);
  for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
    String key = String("reader_") + String(i + 2);
    prefs.putString(key.c_str(), gSettings.remoteReaders[i]);

    String leftToolKey = String("rtool_") + String(i + 2) + "_l";
    String rightToolKey = String("rtool_") + String(i + 2) + "_r";
    prefs.putUChar(leftToolKey.c_str(), gSettings.remoteReaderTools[i][0]);
    prefs.putUChar(rightToolKey.c_str(), gSettings.remoteReaderTools[i][1]);
  }
  prefs.end();
}

static String filamentDetectUrl() {
  return printerBaseUrl() + "/printer/filament_detect/set";
}

static uint8_t printerChannelIndex() {
  return gSettings.channels[activeReaderIndex] < 4 ? gSettings.channels[activeReaderIndex] : 0;
}

static uint32_t printerQueryPhaseOffsetMs() {
  return ((uint32_t)printerChannelIndex() * PRINTER_MOTION_QUERY_MS) / PRINTER_QUERY_STAGGER_SLOTS;
}

static uint32_t printerQueryJitterMs() {
  return PRINTER_QUERY_JITTER_MS ? (uint32_t)random(PRINTER_QUERY_JITTER_MS + 1) : 0;
}

static String printerMotionQueryUrl() {
  String url = printerBaseUrl() + "/printer/objects/query?filament_motion_sensor%20e";
  url += String(printerChannelIndex());
  url += "_filament=filament_detected";
  return url;
}

static String printerFilamentInfoQueryUrl() {
  String url = printerBaseUrl() + "/printer/objects/query?filament_detect=info";
  url += "&filament_motion_sensor%20e";
  url += String(printerChannelIndex());
  url += "_filament=filament_detected";
  return url;
}

static String printerChannelQueryUrl() {
  return printerFilamentInfoQueryUrl();
}

static void storeTagState(const OpenSpoolFields& fields,
                          const String& openspoolJson,
                          const uint8_t* uid,
                          uint8_t uidLen,
                          const String& mappedPayload,
                          const String& fingerprint,
                          const char* source) {
  TagState next;
  next.hasData = true;
  next.active = true;
  next.lastSeenMs = millis();
  next.fingerprint = fingerprint;
  next.uidHex = bytesToHexString(uid, uidLen);
  next.vendor = fields.vendor;
  next.mainType = fields.mainType;
  next.subType = fields.subType;
  next.colorHex = fields.colorHex;
  next.source = source ? source : "";
  next.minTemp = fields.minTemp;
  next.maxTemp = fields.maxTemp;
  next.bedTemp = fields.bedTemp;
  next.openspoolJson = openspoolJson;
  next.mappedPayload = mappedPayload;

  bool changed = !gTagState.hasData ||
                 !gTagState.active ||
                 gTagState.fingerprint != next.fingerprint ||
                 gTagState.source != next.source ||
                 gTagState.openspoolJson != next.openspoolJson ||
                 gTagState.mappedPayload != next.mappedPayload;

  gTagState = next;
  if (changed) bumpStateRevision();
}

static String printerStateFingerprint(const PrinterChannelState& state) {
  String fingerprint;
  fingerprint.reserve(256);
  fingerprint += state.vendor;
  fingerprint += '|';
  fingerprint += state.manufacturer;
  fingerprint += '|';
  fingerprint += state.mainType;
  fingerprint += '|';
  fingerprint += state.subType;
  fingerprint += '|';
  fingerprint += state.colorHex;
  fingerprint += '|';
  fingerprint += state.cardUidCsv;
  fingerprint += '|';
  fingerprint += String(state.minTemp);
  fingerprint += '|';
  fingerprint += String(state.maxTemp);
  fingerprint += '|';
  fingerprint += String(state.bedTemp);
  fingerprint += '|';
  fingerprint += String(state.motionKnown ? (state.filamentDetected ? 1 : 0) : -1);
  fingerprint += '|';
  fingerprint += String(state.queryOk ? 1 : 0);
  fingerprint += '|';
  fingerprint += String(state.hasInfo ? 1 : 0);
  fingerprint += '|';
  fingerprint += state.error;
  return fingerprint;
}

static void commitPrinterState(const PrinterChannelState& next) {
  bool changed = gPrinterState.fingerprint != next.fingerprint ||
                 gPrinterState.queryOk != next.queryOk ||
                 gPrinterState.httpCode != next.httpCode;
  gPrinterState = next;
  if (changed) bumpStateRevision();
}

static void setPrinterStateDisconnected(const char* reason) {
  PrinterChannelState next = gPrinterState;
  next.wifiConnected = false;
  next.queryOk = false;
  next.hasInfo = false;
  next.motionKnown = false;
  next.httpCode = 0;
  next.error = reason ? reason : "";
  next.endpoint = printerMotionQueryUrl();
  next.lastQueryMs = millis();
  next.fingerprint = printerStateFingerprint(next);
  commitPrinterState(next);
}

static bool httpGetPrinterUrl(const String& url, String& resp, int& httpCode, bool logDetails = false) {
  if (WiFi.status() != WL_CONNECTED) {
    setPrinterStateDisconnected("Wi-Fi disconnected");
    return false;
  }

  if (logDetails) {
    Serial.printf("[DEBUG] PRINTER QUERY %s\n", url.c_str());
  }

  HTTPClient http;
  http.setTimeout(PRINTER_QUERY_TIMEOUT_MS);
  if (!http.begin(url)) {
    setPrinterStateDisconnected("HTTP begin failed");
    return false;
  }

  httpCode = http.GET();
  resp = (httpCode > 0) ? http.getString() : "";
  http.end();

  if (logDetails) {
    Serial.printf("[DEBUG] PRINTER QUERY RESULT ok=%d code=%d body=%s\n",
                  httpCode > 0 ? 1 : 0,
                  httpCode,
                  resp.c_str());
  }

  return httpCode > 0;
}

static void markPrinterQueryError(const String& url, int httpCode, const char* reason, bool clearInfo) {
  PrinterChannelState next = gPrinterState;
  next.wifiConnected = true;
  next.queryOk = false;
  if (clearInfo) next.hasInfo = false;
  next.httpCode = httpCode;
  next.error = reason ? reason : "query failed";
  next.endpoint = url;
  next.lastQueryMs = millis();
  next.fingerprint = printerStateFingerprint(next);
  commitPrinterState(next);
}

static bool updatePrinterMotionStateFromJson(const String& rawResponse, int httpCode, const String& url) {
  PrinterChannelState next = gPrinterState;
  next.wifiConnected = true;
  next.httpCode = httpCode;
  next.lastQueryMs = millis();
  next.endpoint = url;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, rawResponse);
  if (err) {
    next.queryOk = false;
    next.error = "motion JSON decode failed";
  } else {
    JsonObject status = doc["result"]["status"];
    String sensorKey = String("filament_motion_sensor e") + String(printerChannelIndex()) + "_filament";
    JsonVariant sensor = status[sensorKey];
    if (!sensor.isNull() && sensor["filament_detected"].is<bool>()) {
      next.queryOk = true;
      next.error = "";
      next.motionKnown = true;
      next.filamentDetected = sensor["filament_detected"].as<bool>();
    } else {
      next.queryOk = false;
      next.error = "motion sensor missing";
    }
  }

  next.fingerprint = printerStateFingerprint(next);
  commitPrinterState(next);
  return gPrinterState.queryOk;
}

static bool updatePrinterInfoStateFromJson(const String& rawResponse, int httpCode, const String& url) {
  PrinterChannelState next = gPrinterState;
  next.wifiConnected = true;
  next.httpCode = httpCode;
  next.lastQueryMs = millis();
  next.endpoint = url;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, rawResponse);
  if (err) {
    next.queryOk = false;
    next.hasInfo = false;
    next.error = "filament JSON decode failed";
    next.rawJson = "";
  } else {
    JsonObject status = doc["result"]["status"];
    JsonArray infoArr = status["filament_detect"]["info"];
    if (!infoArr.isNull() && printerChannelIndex() < infoArr.size()) {
      JsonObject info = infoArr[printerChannelIndex()];
      next.queryOk = true;
      next.hasInfo = !info.isNull();
      next.error = "";
      next.vendor = info["VENDOR"].is<const char*>() ? String(info["VENDOR"].as<const char*>()) : "";
      next.manufacturer = info["MANUFACTURER"].is<const char*>() ? String(info["MANUFACTURER"].as<const char*>()) : "";
      next.mainType = info["MAIN_TYPE"].is<const char*>() ? String(info["MAIN_TYPE"].as<const char*>()) : "";
      next.subType = info["SUB_TYPE"].is<const char*>() ? String(info["SUB_TYPE"].as<const char*>()) : "";
      next.cardUidCsv = uidJsonArrayToCsv(info["CARD_UID"]);
      next.colorHex = "";
      if (info["RGB_1"].is<int>() || info["RGB_1"].is<long>()) {
        next.colorHex = rgbIntToHexString(info["RGB_1"].as<int>());
      }
      next.minTemp = -1;
      next.maxTemp = -1;
      next.bedTemp = -1;
      if (info["HOTEND_MIN_TEMP"].is<int>() || info["HOTEND_MIN_TEMP"].is<long>()) next.minTemp = info["HOTEND_MIN_TEMP"].as<int>();
      if (info["HOTEND_MAX_TEMP"].is<int>() || info["HOTEND_MAX_TEMP"].is<long>()) next.maxTemp = info["HOTEND_MAX_TEMP"].as<int>();
      if (info["BED_TEMP"].is<int>() || info["BED_TEMP"].is<long>()) next.bedTemp = info["BED_TEMP"].as<int>();
      next.officialKnown = false;
      next.official = false;
      if (info["OFFICIAL"].is<bool>()) {
        next.officialKnown = true;
        next.official = info["OFFICIAL"].as<bool>();
      }
      next.rawJson = "";
      serializeJson(info, next.rawJson);
    } else {
      next.queryOk = false;
      next.hasInfo = false;
      next.error = "filament_detect.info missing";
      next.rawJson = "";
    }

    String sensorKey = String("filament_motion_sensor e") + String(printerChannelIndex()) + "_filament";
    JsonVariant sensor = status[sensorKey];
    if (!sensor.isNull() && sensor["filament_detected"].is<bool>()) {
      next.motionKnown = true;
      next.filamentDetected = sensor["filament_detected"].as<bool>();
    }
  }

  next.fingerprint = printerStateFingerprint(next);
  commitPrinterState(next);
  if (gPrinterState.queryOk && gPrinterState.hasInfo) {
    lastPrinterInfoQueryMs[activeReaderIndex] = millis();
  }
  return gPrinterState.queryOk && gPrinterState.hasInfo;
}

static bool fetchPrinterMotionState() {
  String url = printerMotionQueryUrl();
  String resp;
  int httpCode = 0;
  if (!httpGetPrinterUrl(url, resp, httpCode, false)) {
    markPrinterQueryError(url, httpCode, "HTTP motion GET failed", false);
    return false;
  }
  return updatePrinterMotionStateFromJson(resp, httpCode, url);
}

static bool fetchPrinterInfoState(const char* reason) {
  String url = printerFilamentInfoQueryUrl();
  const bool initialQuery = lastPrinterInfoQueryMs[activeReaderIndex] == 0;
  const bool setVerifyQuery = reason && strcmp(reason, "set verify") == 0;
  const bool logDetails = isDebugSerialEnabled() && (initialQuery || setVerifyQuery);
  if (logDetails) {
    Serial.printf("[DEBUG] FILAMENT INFO QUERY reason=%s\n", reason ? reason : "unknown");
  }
  String resp;
  int httpCode = 0;
  if (!httpGetPrinterUrl(url, resp, httpCode, logDetails)) {
    markPrinterQueryError(url, httpCode, "HTTP filament GET failed", true);
    return false;
  }
  return updatePrinterInfoStateFromJson(resp, httpCode, url);
}

static bool fetchPrinterChannelState() {
  return fetchPrinterInfoState("compat");
}

static void scheduleNextMotionQuery(uint32_t now) {
  const bool filamentLoaded = gPrinterState.motionKnown && gPrinterState.filamentDetected;
  const uint32_t intervalMs = filamentLoaded ? PRINTER_LOADED_MOTION_QUERY_MS : PRINTER_MOTION_QUERY_MS;
  nextMotionQueryMs[activeReaderIndex] = now + intervalMs + printerQueryJitterMs();
}

static void scheduleNextInfoSync(uint32_t now) {
  nextInfoSyncMs[activeReaderIndex] = now + PRINTER_INFO_SYNC_MS + printerQueryJitterMs();
}

static void scheduleInitialPrinterQueries(uint32_t now) {
  uint32_t phase = printerQueryPhaseOffsetMs();
  nextMotionQueryMs[activeReaderIndex] = now + phase + printerQueryJitterMs();
  nextInfoSyncMs[activeReaderIndex] = now + phase + 500 + printerQueryJitterMs();
}

static bool ensureFreshPrinterInfo(const char* reason, uint32_t maxAgeMs) {
  if (WiFi.status() != WL_CONNECTED || portalMode) return false;
  if (lastPrinterInfoQueryMs[activeReaderIndex] != 0 && (millis() - lastPrinterInfoQueryMs[activeReaderIndex]) <= maxAgeMs) {
    return gPrinterState.queryOk && gPrinterState.hasInfo;
  }
  bool ok = fetchPrinterInfoState(reason);
  if (ok) scheduleNextInfoSync(millis());
  return ok;
}

static bool currentPrinterMatchesExpected(const String& expectedWithUid, const String& expectedNoUid) {
  if (!gPrinterState.queryOk || !gPrinterState.hasInfo) return false;
  String current = currentPrinterComparableFingerprint();
  return current == expectedWithUid || current == expectedNoUid;
}

static void clearPendingVerification() {
  pendingVerifyPayload[activeReaderIndex] = "";
  pendingVerifyFingerprint[activeReaderIndex] = "";
  pendingVerifyExpectedWithUid[activeReaderIndex] = "";
  pendingVerifyExpectedNoUid[activeReaderIndex] = "";
  pendingVerifyDueMs[activeReaderIndex] = 0;
  pendingVerifyRetriesLeft[activeReaderIndex] = 0;
}

static bool filamentSensorLocksReader(uint8_t reader) {
  return reader < READER_COUNT &&
         gPrinterStates[reader].motionKnown &&
         gPrinterStates[reader].filamentDetected;
}

static bool allowTagSetWithFreshFilamentState() {
  bool motionOk = fetchPrinterMotionState();
  scheduleNextMotionQuery(millis());
  if (!motionOk || !gPrinterState.motionKnown) {
    debugPrint("[DEBUG] SET blocked: current filament sensor state is unavailable");
    return false;
  }
  if (gPrinterState.filamentDetected) {
    clearPendingVerification();
    Serial.printf("[API] SET skipped reason=filament detected reader=%s\n", READER_LABELS[activeReaderIndex]);
    debugPrint("[DEBUG] SET blocked: filament is already loaded at the assigned Tool Head");
    return false;
  }
  return true;
}

static void handleStateApi() {
  String etag = String("\"") + String(stateRevision) + "\"";
  if (web.hasHeader("If-None-Match") && web.header("If-None-Match") == etag) {
    web.sendHeader("ETag", etag);
    web.sendHeader("Cache-Control", "no-cache");
    web.send(304, "text/plain", "");
    return;
  }

  JsonDocument doc;
  doc["revision"] = stateRevision;
  doc["mode"] = portalMode ? "portal" : "station";

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["connected"] = WiFi.status() == WL_CONNECTED;
  wifi["ssid"] = gSettings.wifiSsid;
  wifi["hostname"] = gSettings.hostname;
  wifi["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
  wifi["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  wifi["bssid"] = (WiFi.status() == WL_CONNECTED) ? WiFi.BSSIDstr() : "";
  wifi["bssid_scan_age_ms"] = lastVisibleWifiBssidScanMs ? (millis() - lastVisibleWifiBssidScanMs) : 0;
  JsonArray visibleBssids = wifi["visible_bssids"].to<JsonArray>();
  for (uint8_t i = 0; i < gVisibleWifiBssidCount; i++) {
    JsonObject accessPoint = visibleBssids.add<JsonObject>();
    accessPoint["bssid"] = gVisibleWifiBssids[i].bssid;
    accessPoint["rssi"] = gVisibleWifiBssids[i].rssi;
    accessPoint["channel"] = gVisibleWifiBssids[i].channel;
    accessPoint["connected"] = gVisibleWifiBssids[i].connected;
  }

  JsonObject printer = doc["printer"].to<JsonObject>();
  printer["channel"] = gSettings.channels[activeReaderIndex];
  printer["tool_head"] = gSettings.channels[activeReaderIndex] + 1;
  printer["address"] = gSettings.printerIp;
  printer["address_type"] = printerAddressType(String(gSettings.printerIp));
  printer["port"] = gSettings.printerPort;
  printer["endpoint"] = gPrinterState.endpoint;
  printer["query_ok"] = gPrinterState.queryOk;
  printer["age_ms"] = gPrinterState.lastQueryMs ? (millis() - gPrinterState.lastQueryMs) : 0;
  printer["vendor"] = gPrinterState.vendor;
  printer["manufacturer"] = gPrinterState.manufacturer;
  printer["main_type"] = gPrinterState.mainType;
  printer["sub_type"] = gPrinterState.subType;
  printer["color_hex"] = gPrinterState.colorHex;
  printer["card_uid"] = gPrinterState.cardUidCsv;
  if (gPrinterState.minTemp >= 0) printer["min_temp"] = gPrinterState.minTemp;
  else printer["min_temp"] = nullptr;
  if (gPrinterState.maxTemp >= 0) printer["max_temp"] = gPrinterState.maxTemp;
  else printer["max_temp"] = nullptr;
  if (gPrinterState.bedTemp >= 0) printer["bed_temp"] = gPrinterState.bedTemp;
  else printer["bed_temp"] = nullptr;
  if (gPrinterState.motionKnown) printer["filament_detected"] = gPrinterState.filamentDetected;
  else printer["filament_detected"] = nullptr;
  if (gPrinterState.officialKnown) printer["official"] = gPrinterState.official;
  else printer["official"] = nullptr;
  printer["error"] = gPrinterState.error;
  printer["raw_json"] = gPrinterState.rawJson;

  JsonObject tag = doc["tag"].to<JsonObject>();
  tag["has_data"] = gTagState.hasData;
  tag["active"] = gTagState.active;
  tag["age_ms"] = gTagState.lastSeenMs ? (millis() - gTagState.lastSeenMs) : 0;
  tag["uid"] = gTagState.uidHex;
  tag["source"] = gTagState.source;
  tag["vendor"] = gTagState.vendor;
  tag["main_type"] = gTagState.mainType;
  tag["sub_type"] = gTagState.subType;
  tag["color_hex"] = gTagState.colorHex;
  if (gTagState.minTemp >= 0) tag["min_temp"] = gTagState.minTemp;
  else tag["min_temp"] = nullptr;
  if (gTagState.maxTemp >= 0) tag["max_temp"] = gTagState.maxTemp;
  else tag["max_temp"] = nullptr;
  if (gTagState.bedTemp >= 0) tag["bed_temp"] = gTagState.bedTemp;
  else tag["bed_temp"] = nullptr;
  tag["openspool_json"] = gTagState.openspoolJson;
  tag["mapped_payload"] = gTagState.mappedPayload;

  JsonObject hook = doc["webhook"].to<JsonObject>();
  hook["known"] = gWebhookState.known;
  hook["ok"] = gWebhookState.ok;
  hook["http_code"] = gWebhookState.httpCode;
  hook["age_ms"] = gWebhookState.lastSentMs ? (millis() - gWebhookState.lastSentMs) : 0;
  hook["response"] = gWebhookState.response;

  JsonArray readers = doc["readers"].to<JsonArray>();
  uint8_t previousReader = activeReaderIndex;
  for (uint8_t readerIndex = 0; readerIndex < READER_COUNT; readerIndex++) {
    selectReader(readerIndex);
    JsonObject reader = readers.add<JsonObject>();
    reader["slot"] = readerIndex;
    reader["label"] = READER_LABELS[readerIndex];
    reader["nfc_ready"] = nfcReady[readerIndex];
    reader["channel"] = gSettings.channels[activeReaderIndex];
    reader["tool_head"] = gSettings.channels[activeReaderIndex] + 1;
    JsonObject localPrinter = reader["printer"].to<JsonObject>();
    localPrinter["endpoint"] = gPrinterState.endpoint;
    localPrinter["query_ok"] = gPrinterState.queryOk;
    localPrinter["age_ms"] = gPrinterState.lastQueryMs ? (millis() - gPrinterState.lastQueryMs) : 0;
    localPrinter["vendor"] = gPrinterState.vendor;
    localPrinter["manufacturer"] = gPrinterState.manufacturer;
    localPrinter["main_type"] = gPrinterState.mainType;
    localPrinter["sub_type"] = gPrinterState.subType;
    localPrinter["color_hex"] = gPrinterState.colorHex;
    localPrinter["card_uid"] = gPrinterState.cardUidCsv;
    if (gPrinterState.minTemp >= 0) localPrinter["min_temp"] = gPrinterState.minTemp;
    else localPrinter["min_temp"] = nullptr;
    if (gPrinterState.maxTemp >= 0) localPrinter["max_temp"] = gPrinterState.maxTemp;
    else localPrinter["max_temp"] = nullptr;
    if (gPrinterState.bedTemp >= 0) localPrinter["bed_temp"] = gPrinterState.bedTemp;
    else localPrinter["bed_temp"] = nullptr;
    if (gPrinterState.motionKnown) localPrinter["filament_detected"] = gPrinterState.filamentDetected;
    else localPrinter["filament_detected"] = nullptr;
    if (gPrinterState.officialKnown) localPrinter["official"] = gPrinterState.official;
    else localPrinter["official"] = nullptr;
    localPrinter["error"] = gPrinterState.error;
    localPrinter["raw_json"] = gPrinterState.rawJson;
    JsonObject localTag = reader["tag"].to<JsonObject>();
    localTag["has_data"] = gTagState.hasData;
    localTag["active"] = gTagState.active;
    localTag["age_ms"] = gTagState.lastSeenMs ? (millis() - gTagState.lastSeenMs) : 0;
    localTag["uid"] = gTagState.uidHex;
    localTag["source"] = gTagState.source;
    localTag["vendor"] = gTagState.vendor;
    localTag["main_type"] = gTagState.mainType;
    localTag["sub_type"] = gTagState.subType;
    localTag["color_hex"] = gTagState.colorHex;
    if (gTagState.minTemp >= 0) localTag["min_temp"] = gTagState.minTemp;
    else localTag["min_temp"] = nullptr;
    if (gTagState.maxTemp >= 0) localTag["max_temp"] = gTagState.maxTemp;
    else localTag["max_temp"] = nullptr;
    if (gTagState.bedTemp >= 0) localTag["bed_temp"] = gTagState.bedTemp;
    else localTag["bed_temp"] = nullptr;
    localTag["openspool_json"] = gTagState.openspoolJson;
    localTag["mapped_payload"] = gTagState.mappedPayload;
    JsonObject localHook = reader["webhook"].to<JsonObject>();
    localHook["known"] = gWebhookState.known;
    localHook["ok"] = gWebhookState.ok;
    localHook["http_code"] = gWebhookState.httpCode;
    localHook["age_ms"] = gWebhookState.lastSentMs ? (millis() - gWebhookState.lastSentMs) : 0;
    localHook["response"] = gWebhookState.response;
  }
  selectReader(previousReader);

  String payload;
  serializeJson(doc, payload);
  web.sendHeader("ETag", etag);
  web.sendHeader("Cache-Control", "no-cache");
  web.send(200, "application/json; charset=utf-8", payload);
}

// ------------------------------ Web portal ------------------------------
static String configPageHtml(const String& msg) {
  String body;
  body.reserve(7200);
  body += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  body += F("<title>U1 Argus RFID Setup</title><style>:root{--bg:#f4efe5;--panel:#fffaf1;--ink:#18231e;--muted:#5d6a62;--line:#d8cdb7;--accent:#b85c38;--accent2:#2f5d50}*{box-sizing:border-box}body{font-family:'Trebuchet MS',Verdana,sans-serif;max-width:820px;margin:0 auto;padding:18px 14px 48px;background:radial-gradient(circle at top,#fff9ee 0,#f4efe5 48%,#ece3d3 100%);color:var(--ink)}.shell{background:var(--panel);border:1px solid var(--line);border-radius:22px;padding:22px;box-shadow:0 18px 44px rgba(67,49,26,.10)}label{display:block;margin-top:12px;font-weight:700}input,select{width:100%;padding:12px 13px;margin-top:5px;border:1px solid #cdbfa6;border-radius:12px;background:#fffdf8}input[type=checkbox]{width:auto;margin:0 9px 0 0}.checkRow{display:flex;align-items:center;margin-top:12px;font-weight:700}.warn{display:block;margin-top:6px;padding:10px 12px;border-radius:12px;background:#fff2d6;border:1px solid #e3bd72;color:#6d4a14;line-height:1.35}button,.btn{display:inline-block;margin-top:16px;padding:11px 15px;border:none;border-radius:999px;background:var(--accent);color:#fff;text-decoration:none;font-weight:700}.btn.alt{background:var(--accent2)}small,.muted{color:var(--muted)}code{background:#efe4d0;padding:2px 6px;border-radius:6px}.help{display:block;margin-top:5px;line-height:1.35}.top{display:flex;justify-content:space-between;gap:12px;align-items:center;flex-wrap:wrap}.topRight{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.msg{padding:12px 14px;border-radius:12px;background:#f7e9d4;border:1px solid #e7d0ac;margin:14px 0}.group{margin-top:18px;padding-top:8px;border-top:1px dashed #d8cdb7}</style></head><body>");
  body += F("<div class='shell'>");
  body += F("<div class='top'><div>");
  body += "<h2>U1 Argus Remote RFID - Setup</h2>";
  body += F("<p><small>");
  body += "Firmware: ";
  body += FW_VERSION;
  body += F("</small></p></div>");
  body += F("<div class='topRight'>");
  body += F("<div style='width:100%;text-align:right'><h2 style='margin:0'>");
  body += htmlEscape(configuredMdnsName());
  body += F("</h2></div>");
  if (!portalMode) body += String(F("<a class='btn alt' href='/'>")) + "Dashboard" + F("</a>");
  body += F("</div></div>");
  if (msg.length()) {
    body += F("<div class='msg'><b>");
    body += htmlEscape(msg);
    body += F("</b></div>");
  }
  body += F("<form method='POST' action='/save'>");
  body += String(F("<label>")) + "Wi-Fi SSID" + F("</label><input name='ssid' maxlength='32' required value='"); body += htmlEscape(gSettings.wifiSsid); body += F("'>");
  body += String(F("<label>")) + "Wi-Fi Password" + F("</label><input name='pass' maxlength='64' value='"); body += htmlEscape(gSettings.wifiPass); body += F("'>");
  body += String(F("<label>")) + "Preferred Wi-Fi BSSID 1 (optional)" + F("</label><input name='bssid_1' maxlength='17' placeholder='AA:BB:CC:DD:EE:FF' value='"); body += htmlEscape(gSettings.wifiBssids[0]); body += F("'>");
  body += String(F("<label>")) + "Preferred Wi-Fi BSSID 2 (optional)" + F("</label><input name='bssid_2' maxlength='17' placeholder='AA:BB:CC:DD:EE:FF' value='"); body += htmlEscape(gSettings.wifiBssids[1]); body += F("'><small class='help'>");
  body += "When present, these access points are tried in order. Other access points with the SSID are used only when neither preferred BSSID is visible.";
  body += F("</small>");
  body += String(F("<label>")) + "Hostname (mDNS, no .local)" + F("</label><input name='hostname' maxlength='32' required value='"); body += htmlEscape(gSettings.hostname); body += F("'>");
  body += String(F("<label>")) + "Snapmaker U1 address" + F("</label><input name='printer_ip' maxlength='63' required placeholder='192.168.1.120 or u1.local' value='"); body += htmlEscape(gSettings.printerIp); body += F("'><small class='help'>");
  body += "IP or mDNS hostname, e.g. 192.168.1.120 or u1.local";
  body += F("</small>");
  body += String(F("<label>")) + "Snapmaker U1 Port" + F("</label><input name='printer_port' type='number' min='1' max='65535' required value='"); body += String(gSettings.printerPort); body += F("'>");
  body += F("<div class='group'><label class='checkRow'><input type='checkbox' name='debug_serial' value='1'");
  if (gSettings.debugSerial) body += F(" checked");
  body += F(">Debug serial log</label><small class='warn'>Warning: enabling debug serial logs can slow down NFC tag detection and reading. Leave this off for normal printing.</small></div>");
  for (uint8_t reader = 0; reader < READER_COUNT; reader++) {
    body += String(F("<label>")) + READER_LABELS[reader] + " Tool Head" + F("</label><select name='channel_");
    body += String(reader);
    body += F("'>");
    for (int i = 0; i < 4; i++) {
      body += "<option value='" + String(i) + "'" + String(gSettings.channels[reader] == i ? " selected" : "") + ">";
      body += "Tool Head ";
      body += String(i + 1);
      body += F("</option>");
    }
    body += F("</select>");
  }
  body += String(F("<div class='group'><p><b>")) + "Additional U1 Argus Remote Reader" + F("</b><br><small>") + "Optional: IP or full URL plus the two Tool Heads handled by the other two-spool reader." + F("</small></p>");
  body += String(F("<button type='button' class='btn alt' id='prefillReaders'>")) + "Prefill RFID readers from mDNS name" + F("</button><br><small class='help'>");
  body += "Fills only empty reader fields and skips this reader's own Tool Head.";
  body += F("</small>");
  for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
    body += String(F("<label>")) + "Other reader IP or URL";
    body += F("</label><input name='reader_");
    body += String(i + 2);
    body += F("' maxlength='95' placeholder='");
    body += "e.g. 192.168.1.51 or http://u1-argus-2.local/";
    body += F("' value='");
    body += htmlEscape(gSettings.remoteReaders[i]);
    body += F("'>");

    for (uint8_t slot = 0; slot < READER_COUNT; slot++) {
      body += String(F("<label>Other reader ")) + READER_LABELS[slot] + " Tool Head" + F("</label><select name='reader_tool_");
      body += String(i + 2);
      body += slot == 0 ? F("_left'>") : F("_right'>");
      body += F("<option value='0'");
      if (gSettings.remoteReaderTools[i][slot] == 0) body += F(" selected");
      body += F(">Not assigned</option>");
      for (uint8_t tool = 1; tool <= 4; tool++) {
        if (tool == (uint8_t)(gSettings.channels[0] + 1) || tool == (uint8_t)(gSettings.channels[1] + 1)) continue;
        body += F("<option value='");
        body += String(tool);
        body += F("'");
        if (gSettings.remoteReaderTools[i][slot] == tool) body += F(" selected");
        body += F(">Tool Head ");
        body += String(tool);
        body += F("</option>");
      }
      body += F("</select>");
    }
  }
  body += F("</div>");
  body += String(F("<button type='submit'>")) + "Save & Reboot" + F("</button></form>");
  body += F("<div class='group'><p><b>QIDI Tag Support</b><br><small>");
  body += "Built-in Plus4 defaults are active. Optional: upload officiall_filas_list.cfg to update only material and manufacturer names.";
  body += F("</small></p><p><small>");
  body += "QIDI list: ";
  body += gQidiCustomConfig ? "custom uploaded cfg" : "built-in Plus4 defaults";
  body += " (";
  body += String(gQidiCustomConfig ? gQidiMaterialCount : (sizeof(DEFAULT_QIDI_MATERIALS) / sizeof(DEFAULT_QIDI_MATERIALS[0])));
  body += " materials, ";
  body += String(gQidiCustomConfig ? gQidiVendorCount : (sizeof(DEFAULT_QIDI_VENDORS) / sizeof(DEFAULT_QIDI_VENDORS[0])));
  body += " vendors)";
  if (!gQidiLastSaveOk) body += " - last save failed";
  body += F("<br>QIDI diag: ");
  body += gQidiCustomConfig ? "custom=1" : "custom=0";
  body += ", ram=";
  body += String(gQidiMaterialCount);
  body += "/";
  body += String(gQidiVendorCount);
  body += ", fw=";
  body += FW_VERSION;
  body += ", bytes=";
  body += String(gQidiCfgBytes);
  body += F("</small></p><form method='POST' action='/qidi_cfg' enctype='multipart/form-data'>");
  body += F("<input type='file' name='cfg' accept='.cfg,text/plain'><button type='submit' class='btn alt'>Upload QIDI cfg</button></form>");
  body += F("<form method='POST' action='/qidi_cfg_reset'><button type='submit' class='btn'>Reset QIDI cfg</button></form></div>");
  body += String(F("<p><small>")) + "API target: " + F("<code>/printer/filament_detect/set</code></small></p>");
  body += F("<script>function u1aCleanHost(v){v=(v||'').trim();v=v.replace(/^https?:\\/\\//i,'');v=v.split('/')[0];v=v.split(':')[0];v=v.replace(/\\.local$/i,'');return v;}function u1aRemainingTools(){const local=[...document.querySelectorAll('[name^=channel_]')].map(x=>Number(x.value)+1);return[1,2,3,4].filter(x=>!local.includes(x));}function u1aSyncRemoteTools(){const other=u1aRemainingTools();for(const sel of document.querySelectorAll('[name^=reader_tool_]')){const keep=Number(sel.value);sel.innerHTML='<option value=\"0\">Not assigned</option>'+other.map(t=>`<option value=\"${t}\">Tool Head ${t}</option>`).join('');if(other.includes(keep))sel.value=String(keep);}}function u1aPrefillReaders(){const host=u1aCleanHost(document.querySelector('[name=hostname]').value);if(!host)return;const other=u1aRemainingTools();const input=document.querySelector('[name=reader_2]');if(input&&!input.value.trim())input.value='http://'+host+'-other.local';const left=document.querySelector('[name=reader_tool_2_left]');const right=document.querySelector('[name=reader_tool_2_right]');if(left&&other[0]&&(!left.value||left.value==='0'))left.value=String(other[0]);if(right&&other[1]&&(!right.value||right.value==='0'))right.value=String(other[1]);}for(const sel of document.querySelectorAll('[name^=channel_]'))sel.addEventListener('change',u1aSyncRemoteTools);const prefillBtn=document.getElementById('prefillReaders');if(prefillBtn)prefillBtn.addEventListener('click',u1aPrefillReaders);</script>");
  body += F("</div>");
  body += F("</body></html>");
  return body;
}

static String dashboardPageHtml() {
  String body;
  body.reserve(8000);
  String dashboardUrl = String("http://") + configuredMdnsName();
  String currentToolHead = String("Left: Tool Head ") + String(gSettings.channels[0] + 1) +
                           String("<br>Right: Tool Head ") + String(gSettings.channels[1] + 1);
  body += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  body += F("<title>U1 Argus RFID Status</title><style>:root{--bg:#f6f1e6;--panel:#fffaf2;--ink:#1a241f;--muted:#617068;--line:#d9cdb8;--accent:#b65f3b;--accent2:#2f5d50;--ok:#3f7d4f;--warn:#c2862a;--bad:#b64242}*{box-sizing:border-box}body{margin:0;font-family:'Trebuchet MS',Verdana,sans-serif;background:radial-gradient(circle at top,#fffaf0 0,#f1eadc 44%,#e8decf 100%);color:var(--ink)}main{max-width:1080px;margin:0 auto;padding:18px 14px 40px}.hero{display:grid;grid-template-columns:1.3fr .7fr;gap:14px}.panel{background:var(--panel);border:1px solid var(--line);border-radius:24px;padding:18px;box-shadow:0 18px 44px rgba(67,49,26,.10)}h1,h2,h3,p{margin:0}h1{font-size:1.9rem;line-height:1.05}.sub{margin-top:10px;color:var(--muted)}.meta{display:flex;gap:10px;flex-wrap:wrap;margin-top:16px}.chip{display:inline-flex;align-items:center;gap:8px;padding:8px 12px;border-radius:999px;background:#efe4d0;color:#2b312d;font-weight:700;font-size:.92rem}.dot{width:10px;height:10px;border-radius:50%;background:#bbb}.ok{background:var(--ok)}.warn{background:var(--warn)}.bad{background:var(--bad)}.actions{display:flex;justify-content:flex-start;align-items:flex-start}.readerNav{display:flex;flex-wrap:wrap;gap:10px}.btn{display:inline-block;padding:11px 15px;border-radius:999px;background:var(--accent2);color:#fff;text-decoration:none;font-weight:700}.btn.secondary{background:var(--accent)}.btn.ghost{background:#e9dcc7;color:#30463d}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px;margin-top:14px}.cardTitle{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:14px}.stamp{font-size:.88rem;color:var(--muted)}.kv{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px 14px}.item{padding:10px 12px;border-radius:16px;background:#f6efe2;border:1px solid #e7dbc7}.label{font-size:.78rem;color:var(--muted);text-transform:uppercase;letter-spacing:.05em}.value{margin-top:4px;font-size:1rem;font-weight:700;word-break:break-word}.accent{color:var(--accent2)}.swatch{display:inline-flex;align-items:center;gap:8px}.sw{width:16px;height:16px;border-radius:50%;border:1px solid rgba(0,0,0,.15);background:#ddd}.json{margin-top:14px;padding:14px;border-radius:18px;background:#171c19;color:#d7efe5;white-space:pre-wrap;word-break:break-word;font-family:Consolas,'Courier New',monospace;font-size:.84rem;max-height:240px;overflow:auto}.bssidList{margin-top:14px;padding:12px;border-radius:16px;background:#f6efe2;border:1px solid #e7dbc7}.bssidRows{display:grid;gap:7px;margin-top:9px}.bssidRow{display:flex;justify-content:space-between;align-items:center;gap:12px;font-size:.9rem}.bssidRow strong{font-family:Consolas,'Courier New',monospace;font-size:.9rem;word-break:break-all}.bssidRow.connected strong{color:var(--accent2)}.bssidSignal{white-space:nowrap;color:var(--muted)}.foot{margin-top:14px;color:var(--muted);font-size:.9rem}@media (max-width:860px){.hero,.grid,.kv{grid-template-columns:1fr}.actions{justify-content:flex-start}}</style></head><body><main>");
  body += String(F("<section class='hero'><div class='panel'><h1>U1 Argus Remote RFID</h1><p class='sub'>")) + "Dual-reader status for one two-spool dryer, showing both local PN532 readers and both assigned printer Tool Heads." + F("</p><div class='meta'><span class='chip'><span class='dot warn' id='wifiDot'></span><span id='wifiText'>") + "Checking Wi-Fi" + F("</span></span><span class='chip'><span class='dot ok'></span><span>URL: ");
  body += htmlEscape(dashboardUrl);
  body += String(F("</span></span><span class='chip'><span class='dot warn' id='printerDot'></span><span id='printerText'>")) + "Loading printer status" + F("</span></span><span class='chip'><span class='dot warn' id='tagDot'></span><span id='tagText'>") + "Loading tag status" + F("</span></span></div></div><div class='panel actions'><div class='readerNav'>");
  body += F("<h1 style='width:100%'>");
  body += currentToolHead;
  body += F("</h1>");
  body += String(F("<a class='btn' href='/setup'>")) + "Setup" + F("</a>");
  for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
    String remoteUrl = normalizedReaderUrl(gSettings.remoteReaders[i]);
    if (!remoteUrl.length()) continue;
    body += F("<a class='btn ghost' href='");
    body += htmlEscape(remoteUrl);
    body += F("'>");
    uint8_t remoteLeftTool = gSettings.remoteReaderTools[i][0];
    uint8_t remoteRightTool = gSettings.remoteReaderTools[i][1];
    if (remoteLeftTool >= 1 && remoteLeftTool <= 4 && remoteRightTool >= 1 && remoteRightTool <= 4) {
      body += "Tool Heads ";
      body += String(remoteLeftTool);
      body += " + ";
      body += String(remoteRightTool);
    } else {
      body += "Other reader";
    }
    body += F("</a>");
  }
  body += F("</div></div></section>");
  body += F("<section class='grid' id='readerGrid'></section>");
  body += String(F("<section class='grid'><article class='panel'><div class='cardTitle'><div><h2>Network</h2><p class='stamp'>")) + BOARD_LABEL + F(" status</p></div><span class='chip accent' id='revBadge'>rev -</span></div><div class='kv' id='netKv'></div><div class='bssidList'><div class='label'>Visible 2.4 GHz BSSIDs for this SSID</div><p class='stamp' id='bssidAge'>No scan result yet</p><div class='bssidRows' id='bssidRows'></div></div><p class='foot'>Firmware ");
  body += FW_VERSION;
  body += F("</p></article></section>");
  body += F("<script>");
  body += F("const T={wifiChecking:'Checking Wi-Fi',wifiConnected:'Wi-Fi ',wifiDisconnected:'Wi-Fi disconnected',printerLoading:'Loading printer status',printerResponding:'Printer responding',printerNoStatus:'No printer status',tagLoading:'Loading tag status',tagActive:'Tag actively detected',tagStored:'last tag stored',tagNone:'no tag',noResponse:'No response yet',updatedAgo:'updated ',statusError:'Status error: ',unknown:'unknown',source:'Source',vendor:'Vendor',manufacturer:'Manufacturer',material:'Material',subType:'Sub Type',color:'Color',nozzle:'Nozzle',bed:'Bed',filamentSensor:'Filament Sensor',official:'Official',filamentYes:'Filament detected',filamentNo:'No filament',yes:'yes',no:'no',noPrinterData:'No printer data yet.',lastValidTag:'last valid tag ',noValidTag:'No valid tag read yet',ready:'ready',stored:'stored',noTagData:'No tag data yet.',payloadStatus:'Payload Status',lastSendTry:'last send attempt ',nothingSent:'Nothing sent yet',successful:'successful',failed:'failed',result:'Result',target:'Target',channel:'Channel',httpCode:'HTTP Code',idle:'idle',errorShort:'error',noPayload:'No payload sent yet.',ssid:'SSID',ip:'IP',hostname:'Hostname',rssi:'RSSI',mode:'Mode',bssid:'BSSID',scanUpdated:'scan updated ',noBssidScan:'No scan result yet',connected:'connected',webUpdateError:'Web update error',seconds:' s',minutes:' min',hours:' h'};");
  body += F("let etag='';const q=s=>document.querySelector(s);const esc=s=>String(s??'').replace(/[&<>\"]/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[m]));const age=ms=>{if(ms==null)return'-';const s=Math.max(0,Math.round(ms/1000));if(s<60)return s+T.seconds;const m=Math.floor(s/60);if(m<60)return m+T.minutes;const h=Math.floor(m/60);return h+T.hours;};const kv=rows=>rows.map(r=>`<div class='item'><div class='label'>${esc(r[0])}</div><div class='value'>${r[1]}</div></div>`).join('');const sw=hex=>hex?`<span class='swatch'><span class='sw' style='background:${esc(hex)}'></span>${esc(hex)}</span>`:'-';const badge=(dotSel,textSel,ok,warn,text)=>{const dot=q(dotSel);dot.className='dot '+(ok?'ok':warn?'warn':'bad');q(textSel).textContent=text;};const toolHead=r=>`Tool Head ${r.tool_head} (Channel ${r.channel})`;const printerCard=r=>{const p=r.printer||{};return `<article class='panel'><div class='cardTitle'><div><h2>Printer Tool Head ${esc(r.tool_head)}</h2><p class='stamp'>${p.query_ok?T.updatedAgo+age(p.age_ms):T.statusError+esc(p.error||T.unknown)}</p></div><span class='chip'>${esc(r.label)}</span></div><div class='kv'>${kv([[T.vendor,esc(p.vendor||'-')],[T.manufacturer,esc(p.manufacturer||'-')],[T.material,esc(p.main_type||'-')],[T.subType,esc(p.sub_type||'-')],[T.color,sw(p.color_hex)],[T.nozzle,esc(p.min_temp!=null&&p.max_temp!=null?`${p.min_temp} - ${p.max_temp} C`:'-')],[T.bed,esc(p.bed_temp!=null?`${p.bed_temp} C`:'-')],[T.filamentSensor,esc(p.filament_detected==null?'-':(p.filament_detected?T.filamentYes:T.filamentNo))],[T.official,esc(p.official==null?'-':(p.official?T.yes:T.no))]])}</div><div class='json'>${esc(p.raw_json||T.noPrinterData)}</div></article>`;};const tagCard=r=>{const t=r.tag||{},h=r.webhook||{};return `<article class='panel'><div class='cardTitle'><div><h2>Tag Reader Tool Head ${esc(r.tool_head)}</h2><p class='stamp'>${t.has_data?T.lastValidTag+age(t.age_ms):T.noValidTag}</p></div><span class='chip'>${t.active?T.ready:(t.has_data?T.tagStored:T.tagNone)}</span></div><div class='kv'>${kv([['UID',esc(t.uid||'-')],[T.source,esc(t.source||'-')],[T.vendor,esc(t.vendor||'-')],[T.material,esc(t.main_type||'-')],[T.subType,esc(t.sub_type||'-')],[T.color,sw(t.color_hex)],[T.nozzle,esc(t.min_temp!=null&&t.max_temp!=null?`${t.min_temp} - ${t.max_temp} C`:'-')],[T.bed,esc(t.bed_temp!=null?`${t.bed_temp} C`:'-')],[T.result,esc(h.known?(h.ok?T.successful:T.failed):T.nothingSent)]])}</div><div class='json'>${esc(t.openspool_json||T.noTagData)}</div></article>`;};function render(d){const rs=d.readers||[];q('#revBadge').textContent='rev '+d.revision;badge('#wifiDot','#wifiText',d.wifi.connected,!d.wifi.connected,d.wifi.connected?(T.wifiConnected+(d.wifi.ip||'')):T.wifiDisconnected);const allPrinter=rs.length&&rs.every(r=>r.printer?.query_ok),somePrinter=rs.some(r=>r.printer?.query_ok),activeTags=rs.filter(r=>r.tag?.active).length,storedTags=rs.filter(r=>r.tag?.has_data).length;badge('#printerDot','#printerText',allPrinter,somePrinter,allPrinter?T.printerResponding:(somePrinter?'Printer partly responding':T.printerNoStatus));badge('#tagDot','#tagText',activeTags>0,storedTags>0,activeTags?`${activeTags} active tag${activeTags>1?'s':''}`:(storedTags?T.tagStored:T.tagNone));q('#readerGrid').innerHTML=rs.map(r=>printerCard(r)+tagCard(r)).join('');q('#netKv').innerHTML=kv([[T.ssid,esc(d.wifi.ssid||'-')],[T.ip,esc(d.wifi.ip||'-')],[T.hostname,esc(d.wifi.hostname||'-')],[T.rssi,esc(d.wifi.rssi==null?'-':`${d.wifi.rssi} dBm`)],[T.mode,esc(d.mode||'-')],[T.bssid,esc(d.wifi.bssid||'-')]]);const aps=d.wifi.visible_bssids||[];q('#bssidAge').textContent=aps.length?T.scanUpdated+age(d.wifi.bssid_scan_age_ms):T.noBssidScan;q('#bssidRows').innerHTML=aps.map(ap=>`<div class='bssidRow${ap.connected?' connected':''}'><strong>${esc(ap.bssid)}${ap.connected?' *':''}</strong><span class='bssidSignal'>${esc(`${ap.rssi} dBm / ch ${ap.channel}`)}</span></div>`).join('');}async function refresh(){try{const h={};if(etag)h['If-None-Match']=etag;const r=await fetch('/api/state',{cache:'no-store',headers:h});if(r.status===304)return;if(!r.ok)throw new Error('HTTP '+r.status);etag=r.headers.get('ETag')||etag;render(await r.json());}catch(e){q('#printerText').textContent=T.webUpdateError;q('#printerDot').className='dot bad';}}refresh();setInterval(refresh,2000);setInterval(()=>location.reload(),30000);</script>");
  body += F("</main></body></html>");
  return body;
}

static void handleQidiCfgUpload() {
  Serial.printf("[QIDI] upload complete: bytes=%u too_large=%u\n",
                (unsigned)qidiCfgUploadBuffer.length(),
                qidiCfgUploadTooLarge ? 1 : 0);

  if (qidiCfgUploadTooLarge) {
    qidiCfgUploadBuffer = "";
    qidiCfgUploadTooLarge = false;
    sendHtmlNoCache(413, configPageHtml("QIDI cfg upload is too large."));
    return;
  }
  if (qidiCfgUploadBuffer.length() == 0) {
    sendHtmlNoCache(400, configPageHtml("QIDI cfg upload failed. No file data received."));
    return;
  }

  uint8_t materials = 0;
  uint8_t vendors = 0;
  bool ok = importQidiCfgText(qidiCfgUploadBuffer, materials, vendors);
  qidiCfgUploadBuffer = "";
  if (!ok) {
    Serial.printf("[QIDI] cfg import failed: materials=%u vendors=%u\n", (unsigned)materials, (unsigned)vendors);
    if ((materials > 0 || vendors > 0) && !gQidiLastSaveOk) {
      sendHtmlNoCache(500, configPageHtml("QIDI cfg parsed, but saving to persistent storage failed."));
    } else {
      sendHtmlNoCache(400, configPageHtml("QIDI cfg import failed. No material or vendor values found."));
    }
    return;
  }

  Serial.printf("[QIDI] cfg import ok: materials=%u vendors=%u\n", (unsigned)materials, (unsigned)vendors);
  loadQidiConfig();
  if (gQidiMaterialCount != materials || gQidiVendorCount != vendors) {
    Serial.printf("[QIDI] cfg verify failed: loaded materials=%u/%u vendors=%u/%u\n",
                  (unsigned)gQidiMaterialCount,
                  (unsigned)materials,
                  (unsigned)gQidiVendorCount,
                  (unsigned)vendors);
    sendHtmlNoCache(500, configPageHtml("QIDI cfg saved, but persistent verification failed."));
    return;
  }

  String msg = "QIDI cfg imported: ";
  msg += String(materials);
  msg += " materials, ";
  msg += String(vendors);
  msg += " vendors.";
  sendHtmlNoCache(200, configPageHtml(msg));
}

static void handleQidiCfgUploadChunk() {
  HTTPUpload& upload = web.upload();
  if (upload.status == UPLOAD_FILE_START) {
    qidiCfgUploadBuffer = "";
    qidiCfgUploadBuffer.reserve(16000);
    qidiCfgUploadTooLarge = false;
    Serial.printf("[QIDI] upload start: %s\n", upload.filename.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (qidiCfgUploadBuffer.length() + upload.currentSize > QIDI_CFG_UPLOAD_MAX_BYTES) {
      qidiCfgUploadTooLarge = true;
      return;
    }
    for (size_t i = 0; i < upload.currentSize; i++) {
      qidiCfgUploadBuffer += (char)upload.buf[i];
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    Serial.printf("[QIDI] upload end: received=%u bytes\n", (unsigned)qidiCfgUploadBuffer.length());
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    qidiCfgUploadBuffer = "";
    qidiCfgUploadTooLarge = false;
    Serial.println("[QIDI] upload aborted");
  }
}

static void handleQidiCfgReset() {
  resetQidiConfig();
  sendHtmlNoCache(200, configPageHtml("QIDI cfg reset. Built-in Plus4 defaults are active."));
}

static void setupPortalRoutes() {
  if (portalRoutesReady) return;
  portalRoutesReady = true;
  const char* headerKeys[] = {"If-None-Match"};
  web.collectHeaders(headerKeys, 1);

  web.on("/", HTTP_GET, []() {
    sendPortalLanding();
  });
  web.on("/status", HTTP_GET, []() {
    sendHtmlNoCache(200, dashboardPageHtml());
  });
  web.on("/setup", HTTP_GET, []() {
    sendHtmlNoCache(200, configPageHtml());
  });
  web.on("/api/state", HTTP_GET, []() {
    handleStateApi();
  });
  web.on("/qidi_cfg", HTTP_POST, handleQidiCfgUpload, handleQidiCfgUploadChunk);
  web.on("/qidi_cfg_reset", HTTP_POST, handleQidiCfgReset);

  web.on("/generate_204", HTTP_GET, []() {
    if (portalMode) sendPortalRedirect();
    else web.send(204, "text/plain", "");
  });
  web.on("/gen_204", HTTP_GET, []() {
    if (portalMode) sendPortalRedirect();
    else web.send(204, "text/plain", "");
  });
  web.on("/hotspot-detect.html", HTTP_GET, []() {
    if (portalMode) sendPortalLanding();
    else web.send(200, "text/plain", "Success");
  });
  web.on("/library/test/success.html", HTTP_GET, []() {
    if (portalMode) sendPortalLanding();
    else web.send(200, "text/plain", "success");
  });
  web.on("/connecttest.txt", HTTP_GET, []() {
    if (portalMode) sendPortalLanding();
    else web.send(200, "text/plain", "Microsoft Connect Test");
  });
  web.on("/redirect", HTTP_GET, []() {
    if (portalMode) sendPortalLanding();
    else web.send(200, "text/plain", "");
  });
  web.on("/canonical.html", HTTP_GET, []() {
    if (portalMode) sendPortalLanding();
    else web.send(200, "text/plain", "");
  });
  web.on("/ncsi.txt", HTTP_GET, []() {
    if (portalMode) {
      sendPortalRedirect();
    } else {
      web.send(200, "text/plain", "Microsoft NCSI");
    }
  });
  web.on("/fwlink", HTTP_GET, []() {
    if (portalMode) sendPortalLanding();
    else web.send(200, "text/plain", "");
  });

  web.on("/save", HTTP_POST, []() {
    String ssid = web.arg("ssid"); ssid.trim();
    String pass = web.arg("pass");
    char wifiBssids[WIFI_BSSID_COUNT][18] = {{0}};
    for (uint8_t i = 0; i < WIFI_BSSID_COUNT; i++) {
      String bssid = web.arg(String("bssid_") + String(i + 1));
      if (!normalizeBssid(bssid, wifiBssids[i], sizeof(wifiBssids[i]))) {
        sendHtmlNoCache(400, configPageHtml("Wi-Fi BSSID must use format AA:BB:CC:DD:EE:FF."));
        return;
      }
    }
    String hostname = web.arg("hostname"); hostname.trim();
    String printerIp = web.arg("printer_ip"); printerIp.trim();
    printerIp = normalizedHostAddress(printerIp);
    String portStr = web.arg("printer_port"); portStr.trim();
    bool debugSerial = web.hasArg("debug_serial");
    String channelStr[READER_COUNT];
    int channels[READER_COUNT];
    for (uint8_t reader = 0; reader < READER_COUNT; reader++) {
      channelStr[reader] = web.arg(String("channel_") + String(reader));
      channelStr[reader].trim();
      channels[reader] = channelStr[reader].toInt();
    }
    String readerUrls[REMOTE_READER_COUNT];
    uint8_t readerTools[REMOTE_READER_COUNT][READER_COUNT];
    for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
      readerUrls[i] = web.arg(String("reader_") + String(i + 2));
      readerUrls[i].trim();

      for (uint8_t slot = 0; slot < READER_COUNT; slot++) {
        String suffix = slot == 0 ? "_left" : "_right";
        String toolStr = web.arg(String("reader_tool_") + String(i + 2) + suffix);
        toolStr.trim();
        int tool = toolStr.toInt();
        readerTools[i][slot] = (tool >= 1 && tool <= 4) ? (uint8_t)tool : 0;
      }
    }

    uint32_t portVal = portStr.toInt();

    if (ssid.length() == 0) {
      sendHtmlNoCache(400, configPageHtml("SSID must not be empty."));
      return;
    }
    if (wifiBssids[0][0] != '\0' && strcmp(wifiBssids[0], wifiBssids[1]) == 0) {
      sendHtmlNoCache(400, configPageHtml("Preferred Wi-Fi BSSIDs must be different."));
      return;
    }
    if (hostname.length() == 0) {
      sendHtmlNoCache(400, configPageHtml("Hostname must not be empty."));
      return;
    }
    for (uint8_t reader = 0; reader < READER_COUNT; reader++) {
      if (channels[reader] < 0 || channels[reader] > 3) {
        sendHtmlNoCache(400, configPageHtml("Tool Head selection is invalid."));
        return;
      }
    }
    if (channels[0] == channels[1]) {
      sendHtmlNoCache(400, configPageHtml("Left and right spool must use different Tool Heads."));
      return;
    }
    if (readerUrls[0].length()) {
      if (readerTools[0][0] == 0 || readerTools[0][1] == 0 || readerTools[0][0] == readerTools[0][1]) {
        sendHtmlNoCache(400, configPageHtml("The other reader needs two different Tool Heads."));
        return;
      }
    }
    if (portVal == 0 || portVal > 65535 || !parseIpPort(printerIp.c_str(), (uint16_t)portVal)) {
      sendHtmlNoCache(400, configPageHtml("Printer address/port is invalid."));
      return;
    }

    safeCopy(gSettings.wifiSsid, sizeof(gSettings.wifiSsid), ssid);
    safeCopy(gSettings.wifiPass, sizeof(gSettings.wifiPass), pass);
    for (uint8_t i = 0; i < WIFI_BSSID_COUNT; i++) {
      safeCopy(gSettings.wifiBssids[i], sizeof(gSettings.wifiBssids[i]), String(wifiBssids[i]));
    }
    safeCopy(gSettings.hostname, sizeof(gSettings.hostname), hostname);
    safeCopy(gSettings.printerIp, sizeof(gSettings.printerIp), printerIp);
    cachedPrinterAddress = "";
    cachedPrinterHost = "";
    cachedPrinterResolveOk = false;
    gSettings.printerPort = (uint16_t)portVal;
    gSettings.debugSerial = debugSerial;
    gDebugSerialEnabled = debugSerial;
    for (uint8_t reader = 0; reader < READER_COUNT; reader++) {
      gSettings.channels[reader] = (uint8_t)channels[reader];
    }
    for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
      safeCopy(gSettings.remoteReaders[i], sizeof(gSettings.remoteReaders[i]), readerUrls[i]);
      for (uint8_t slot = 0; slot < READER_COUNT; slot++) {
        gSettings.remoteReaderTools[i][slot] = readerTools[i][slot];
      }
    }

    saveSettings();

    String dashboardUrl = String("http://") + configuredMdnsName() + "/";
    String ok;
    ok.reserve(520);
    ok += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
    ok += F("<meta http-equiv='refresh' content='10;url=");
    ok += htmlEscape(dashboardUrl);
    ok += F("'><title>");
    ok += "Saved";
    ok += F("</title></head><body><h3>");
    ok += "Saved. Rebooting...";
    ok += F("</h3><p>");
    ok += "The dashboard should reopen automatically in about 10 seconds.";
    ok += F("</p><p><a href='");
    ok += htmlEscape(dashboardUrl);
    ok += F("'>");
    ok += "Open dashboard now";
    ok += F("</a></p></body></html>");
    sendHtmlNoCache(200, ok);
    delay(500);
    ESP.restart();
  });

  web.onNotFound([]() {
    if (portalMode) {
      sendPortalRedirect();
    } else {
      web.send(404, "text/plain", "Not found");
    }
  });
}

static uint16_t deviceShortId() {
  return (uint16_t)(ESP.getEfuseMac() & 0xFFFF);
}

static const char* setupApSsid() {
  if (apSsid[0] == '\0') {
    snprintf(apSsid, sizeof(apSsid), "%s-%04X", AP_SSID_BASE, (unsigned)deviceShortId());
  }
  return apSsid;
}

static uint32_t wifiStartupStaggerMs() {
  return (uint32_t)(deviceShortId() % WIFI_STARTUP_STAGGER_MAX_MS);
}

static bool waitForWifiConnect(uint32_t timeoutMs, bool servicePortal) {
  uint32_t t0 = millis();
  uint32_t lastDotMs = 0;
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    if (servicePortal) {
      dnsServer.processNextRequest();
      web.handleClient();
    }

    if (millis() - lastDotMs >= 500) {
      lastDotMs = millis();
      Serial.print('.');
    }
    delay(50);
  }
  return WiFi.status() == WL_CONNECTED;
}

static bool hasConfiguredPreferredBssid() {
  for (uint8_t i = 0; i < WIFI_BSSID_COUNT; i++) {
    if (gSettings.wifiBssids[i][0] != '\0') return true;
  }
  return false;
}

static void clearVisibleWifiBssidCache() {
  bool hadCachedResults = gVisibleWifiBssidCount != 0 || lastVisibleWifiBssidScanMs != 0;
  gVisibleWifiBssidCount = 0;
  lastVisibleWifiBssidScanMs = 0;
  visibleWifiBssidStateFingerprint = "";
  if (hadCachedResults) bumpStateRevision();
}

static bool refreshVisibleWifiBssids(bool forceSerialOutput) {
  if (portalMode || WiFi.status() != WL_CONNECTED || strlen(gSettings.wifiSsid) == 0) return false;

  int16_t networkCount = WiFi.scanNetworks(false, true);
  if (networkCount < 0) {
    Serial.printf("[WIFI] Visible BSSID scan failed, result=%d\n", (int)networkCount);
    WiFi.scanDelete();
    return false;
  }

  VisibleWifiBssidState next[WIFI_VISIBLE_BSSID_MAX];
  uint8_t nextCount = 0;
  const String connectedBssid = WiFi.BSSIDstr();
  for (int16_t network = 0; network < networkCount && nextCount < WIFI_VISIBLE_BSSID_MAX; network++) {
    if (WiFi.SSID(network) != String(gSettings.wifiSsid)) continue;
    const uint8_t* bssidBytes = WiFi.BSSID(network);
    if (!bssidBytes) continue;

    char bssidText[18];
    snprintf(bssidText,
             sizeof(bssidText),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             bssidBytes[0],
             bssidBytes[1],
             bssidBytes[2],
             bssidBytes[3],
             bssidBytes[4],
             bssidBytes[5]);
    next[nextCount].bssid = String(bssidText);
    next[nextCount].rssi = WiFi.RSSI(network);
    next[nextCount].channel = WiFi.channel(network);
    next[nextCount].connected = next[nextCount].bssid.equalsIgnoreCase(connectedBssid);
    nextCount++;
  }
  WiFi.scanDelete();

  bool inventoryChanged = nextCount != gVisibleWifiBssidCount;
  if (!inventoryChanged) {
    for (uint8_t i = 0; i < nextCount && !inventoryChanged; i++) {
      bool matched = false;
      for (uint8_t previous = 0; previous < gVisibleWifiBssidCount; previous++) {
        if (next[i].bssid.equalsIgnoreCase(gVisibleWifiBssids[previous].bssid) &&
            next[i].connected == gVisibleWifiBssids[previous].connected) {
          matched = true;
          break;
        }
      }
      inventoryChanged = !matched;
    }
  }

  String nextStateFingerprint;
  for (uint8_t i = 0; i < nextCount; i++) {
    nextStateFingerprint += next[i].bssid;
    nextStateFingerprint += ':';
    nextStateFingerprint += String(next[i].rssi);
    nextStateFingerprint += ':';
    nextStateFingerprint += String(next[i].channel);
    nextStateFingerprint += next[i].connected ? ":1;" : ":0;";
  }
  bool stateChanged = nextStateFingerprint != visibleWifiBssidStateFingerprint;
  for (uint8_t i = 0; i < nextCount; i++) gVisibleWifiBssids[i] = next[i];
  gVisibleWifiBssidCount = nextCount;
  lastVisibleWifiBssidScanMs = millis();
  visibleWifiBssidStateFingerprint = nextStateFingerprint;

  if (forceSerialOutput || inventoryChanged) {
    Serial.printf("[WIFI] Visible 2.4 GHz BSSIDs for SSID=%s (%u found)\n",
                  gSettings.wifiSsid,
                  (unsigned)gVisibleWifiBssidCount);
    for (uint8_t i = 0; i < gVisibleWifiBssidCount; i++) {
      Serial.printf("[WIFI]   BSSID=%s RSSI=%ld dBm channel=%ld%s\n",
                    gVisibleWifiBssids[i].bssid.c_str(),
                    (long)gVisibleWifiBssids[i].rssi,
                    (long)gVisibleWifiBssids[i].channel,
                    gVisibleWifiBssids[i].connected ? " connected" : "");
    }
  }

  if (stateChanged) bumpStateRevision();
  return true;
}

static void maintainVisibleWifiBssidScan() {
  if (portalMode || WiFi.status() != WL_CONNECTED) return;
  for (uint8_t reader = 0; reader < READER_COUNT; reader++) {
    if (gTagStates[reader].active) return;
  }
  if (lastVisibleWifiBssidScanMs != 0 &&
      (millis() - lastVisibleWifiBssidScanMs) < WIFI_VISIBLE_BSSID_REFRESH_MS) {
    return;
  }
  if (refreshVisibleWifiBssids(visibleWifiScanSerialPending)) {
    visibleWifiScanSerialPending = false;
  }
}

static uint8_t scanPreferredWifiTargets(uint8_t targetBssids[][6],
                                        int32_t* targetChannels,
                                        uint8_t* targetPreferences,
                                        bool& scanOk) {
  scanOk = false;
  if (!targetBssids || !targetChannels || !targetPreferences) return 0;

  Serial.printf("[WIFI] Scanning for preferred BSSID access points for SSID=%s\n", gSettings.wifiSsid);
  int16_t networkCount = WiFi.scanNetworks(false, true);
  if (networkCount < 0) {
    Serial.printf("[WIFI] Preferred BSSID scan failed, result=%d\n", (int)networkCount);
    WiFi.scanDelete();
    return 0;
  }
  scanOk = true;

  uint8_t targetCount = 0;
  for (uint8_t preference = 0; preference < WIFI_BSSID_COUNT; preference++) {
    if (gSettings.wifiBssids[preference][0] == '\0') continue;

    uint8_t requested[6] = {0};
    if (!parseBssid(gSettings.wifiBssids[preference], requested)) continue;
    for (int16_t network = 0; network < networkCount; network++) {
      const uint8_t* seen = WiFi.BSSID(network);
      if (!seen || memcmp(requested, seen, sizeof(requested)) != 0) continue;
      String seenSsid = WiFi.SSID(network);
      if (seenSsid.length() && seenSsid != String(gSettings.wifiSsid)) continue;

      memcpy(targetBssids[targetCount], requested, sizeof(requested));
      targetChannels[targetCount] = WiFi.channel(network);
      targetPreferences[targetCount] = preference;
      targetCount++;
      break;
    }
  }
  WiFi.scanDelete();
  return targetCount;
}

static bool connectConfiguredWifiWithPriority(uint32_t timeoutMs, bool servicePortal) {
  if (!hasConfiguredPreferredBssid()) {
    WiFi.begin(gSettings.wifiSsid, gSettings.wifiPass);
    return waitForWifiConnect(timeoutMs, servicePortal);
  }

  uint8_t targetBssids[WIFI_BSSID_COUNT][6] = {{0}};
  int32_t targetChannels[WIFI_BSSID_COUNT] = {0};
  uint8_t targetPreferences[WIFI_BSSID_COUNT] = {0};
  bool scanOk = false;
  uint8_t targetCount = scanPreferredWifiTargets(targetBssids, targetChannels, targetPreferences, scanOk);
  if (!scanOk) {
    Serial.println("[WIFI] Preferred BSSID visibility unknown; unpinned SSID fallback suppressed");
    return false;
  }

  if (targetCount == 0) {
    Serial.println("[WIFI] No preferred BSSID visible; connecting through another access point with the configured SSID");
    WiFi.begin(gSettings.wifiSsid, gSettings.wifiPass);
    return waitForWifiConnect(timeoutMs, servicePortal);
  }

  uint32_t connectStartMs = millis();
  for (uint8_t i = 0; i < targetCount; i++) {
    uint32_t elapsedMs = millis() - connectStartMs;
    if (elapsedMs >= timeoutMs) return false;
    uint32_t remainingMs = timeoutMs - elapsedMs;
    uint32_t attemptMs = remainingMs / (uint32_t)(targetCount - i);

    Serial.printf("[WIFI] Connecting via preferred BSSID %u=%s on channel %ld, timeout=%lu ms\n",
                  (unsigned)(targetPreferences[i] + 1),
                  gSettings.wifiBssids[targetPreferences[i]],
                  (long)targetChannels[i],
                  (unsigned long)attemptMs);
    WiFi.disconnect(false, false);
    delay(20);
    WiFi.begin(gSettings.wifiSsid,
               gSettings.wifiPass,
               targetChannels[i],
               targetBssids[i],
               true);
    if (waitForWifiConnect(attemptMs, servicePortal)) return true;
  }

  Serial.println("\n[WIFI] Visible preferred BSSID connection attempts failed; unpinned SSID fallback suppressed");
  return false;
}

static void beginStationServices(const char* reason) {
  if (portalMode) {
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    portalMode = false;
    Serial.printf("\n[PORTAL] Setup hotspot stopped after Wi-Fi recovery (%s)\n", reason ? reason : "connected");
  }

  WiFi.setAutoReconnect(!hasConfiguredPreferredBssid());
  Serial.printf("\n[WIFI] Connected: %s, BSSID=%s\n",
                WiFi.localIP().toString().c_str(),
                WiFi.BSSIDstr().c_str());
  Serial.printf("[WIFI] RSSI=%d dBm, mode=station\n", (int)WiFi.RSSI());
  debugPrintf("[DEBUG] WiFi connected, hostname=%s, target=%s:%u\n",
              gSettings.hostname,
              gSettings.printerIp,
              gSettings.printerPort);

  if (strlen(gSettings.hostname) > 0) {
    if (!mdnsRunning) {
      if (MDNS.begin(gSettings.hostname)) {
        mdnsRunning = true;
        Serial.printf("[MDNS] http://%s.local/\n", gSettings.hostname);
      } else {
        Serial.println("[MDNS] start failed");
      }
    } else {
      Serial.printf("[MDNS] http://%s.local/\n", gSettings.hostname);
    }
  }

  setupPortalRoutes();
  dnsServer.stop();
  web.begin();
  gPrinterState.wifiConnected = true;
  wifiLostSinceMs = 0;
  lastWifiReconnectMs = 0;
  lastWifiRecoveryMs = 0;
  clearVisibleWifiBssidCache();
  visibleWifiScanSerialPending = true;
}

static void startPortalAp(bool keepStationMode) {
  const bool stationRecoveryEnabled = keepStationMode && strlen(gSettings.wifiSsid) > 0;
  portalMode = true;
  if (stationRecoveryEnabled) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_AP);
  }
  WiFi.softAP(setupApSsid(), AP_PASS);
  setPrinterStateDisconnected("Setup portal active");

  setupPortalRoutes();
  dnsServer.stop();
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  web.begin();

  IPAddress apIp = WiFi.softAPIP();
  Serial.printf("[PORTAL] AP started: SSID=%s, IP=%s%s\n",
                setupApSsid(),
                apIp.toString().c_str(),
                stationRecoveryEnabled ? " while configured Wi-Fi recovery is active" : "");
  Serial.printf("[PORTAL] Captive portal: http://%s/\n", apIp.toString().c_str());
  debugPrintf("[DEBUG] Setup portal active, captive DNS on %s:%u\n",
              apIp.toString().c_str(),
              DNS_PORT);

  if (stationRecoveryEnabled) {
    Serial.printf("[WIFI] Portal recovery enabled for SSID=%s\n", gSettings.wifiSsid);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    lastWifiRecoveryMs = 0;
  }
}

static void startPortal() {
  startPortalAp(strlen(gSettings.wifiSsid) > 0);
}

// ------------------------------ STA mode ------------------------------
static bool connectSta() {
  if (strlen(gSettings.wifiSsid) == 0) return false;

  uint32_t staggerMs = wifiStartupStaggerMs();
  if (staggerMs > 0) {
    Serial.printf("[WIFI] Startup stagger=%lu ms\n", (unsigned long)staggerMs);
    delay(staggerMs);
  }

  Serial.printf("[WIFI] Connecting to SSID=%s, timeout=%lu ms\n",
                gSettings.wifiSsid,
                (unsigned long)WIFI_CONNECT_TIMEOUT_MS);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false);

  if (!connectConfiguredWifiWithPriority(WIFI_CONNECT_TIMEOUT_MS, false)) {
    Serial.printf("\n[WIFI] Connection failed, status=%d\n", (int)WiFi.status());
    Serial.printf("[PORTAL] Configured Wi-Fi fallback allowed after %lu ms\n",
                  (unsigned long)WIFI_PORTAL_FALLBACK_MS);
    return false;
  }

  beginStationServices("initial connect");
  return true;
}

static bool recoverConfiguredWifiFromPortal() {
  if (!portalMode || strlen(gSettings.wifiSsid) == 0) return false;

  if (WiFi.status() == WL_CONNECTED) {
    beginStationServices("portal recovery");
    return true;
  }

  uint32_t now = millis();
  if (lastWifiRecoveryMs != 0 && (now - lastWifiRecoveryMs) < WIFI_RECOVERY_RETRY_MS) return false;
  lastWifiRecoveryMs = now;

  Serial.printf("[WIFI] Portal recovery retry: SSID=%s, timeout=%lu ms\n",
                gSettings.wifiSsid,
                (unsigned long)WIFI_RECOVERY_ATTEMPT_MS);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false);

  if (connectConfiguredWifiWithPriority(WIFI_RECOVERY_ATTEMPT_MS, true)) {
    beginStationServices("portal recovery");
    return true;
  }

  Serial.printf("\n[WIFI] Portal recovery still waiting, status=%d\n", (int)WiFi.status());
  return false;
}

static void maintainWifiConnection() {
  if (strlen(gSettings.wifiSsid) == 0) return;

  if (portalMode) {
    recoverConfiguredWifiFromPortal();
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (wifiLostSinceMs != 0) {
      Serial.printf("[WIFI] Reconnected: %s\n", WiFi.localIP().toString().c_str());
      gPrinterState.wifiConnected = true;
    }
    wifiLostSinceMs = 0;
    return;
  }

  uint32_t now = millis();
  if (wifiLostSinceMs == 0) {
    wifiLostSinceMs = now;
    setPrinterStateDisconnected("Wi-Fi disconnected");
    clearVisibleWifiBssidCache();
    Serial.printf("[WIFI] Lost connection, status=%d\n", (int)WiFi.status());
  }

  if (lastWifiReconnectMs == 0 || (now - lastWifiReconnectMs) >= WIFI_RECOVERY_RETRY_MS) {
    lastWifiReconnectMs = now;
    Serial.printf("[WIFI] Reconnect retry: SSID=%s\n", gSettings.wifiSsid);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(false);
    if (connectConfiguredWifiWithPriority(WIFI_RECOVERY_ATTEMPT_MS, false)) {
      beginStationServices("reconnect");
      return;
    }
  }

  if ((now - wifiLostSinceMs) >= WIFI_PORTAL_FALLBACK_MS) {
    Serial.printf("[PORTAL] Wi-Fi unavailable for %lu ms, starting setup hotspot fallback\n",
                  (unsigned long)WIFI_PORTAL_FALLBACK_MS);
    startPortalAp(true);
    wifiLostSinceMs = 0;
  }
}

// ------------------------------ NFC/OpenSpool parsing ------------------------------
static bool readPageRetry(uint8_t page, uint8_t* out) {
  for (uint8_t i = 0; i < 5; i++) {
    if (nfc.ntag2xx_ReadPage(page, out)) {
      if (i > 0) debugPrintf("[DEBUG] NTAG read recovered on retry at page=%u\n", page);
      return true;
    }
    if (nfc.mifareultralight_ReadPage(page, out)) {
      debugPrintf("[DEBUG] Read page=%u via mifareultralight fallback\n", page);
      return true;
    }
    delay(20);
  }
  debugPrintf("[DEBUG] NTAG read failed at page=%u\n", page);
  return false;
}

static bool readOpenSpoolUserAreaFast(uint8_t* out, size_t outLen, size_t& actualLen) {
  uint32_t readStartMs = millis();
  if (outLen < NTAG_USER_BYTES) return false;
  memset(out, 0x00, outLen);
  actualLen = 0;

  uint8_t data[NTAG_FAST_READ_PAGES * 4] = {0};
  uint8_t pageCount = 0;
  uint8_t firstEndPage = (uint8_t)(NTAG_CC_PAGE + NTAG_INITIAL_FAST_READ_PAGES - 1);
  if (!pn532FastReadPages(NTAG_CC_PAGE, firstEndPage, data, sizeof(data), pageCount)) {
    return false;
  }
  if (pageCount < 4) return false;

  uint16_t lastPage = 0;
  if (!ntagLastUserPageFromCc(data, lastPage)) {
    return false;
  }

  uint16_t endPage = (uint16_t)(NTAG_CC_PAGE + pageCount - 1u);
  if (endPage > lastPage) endPage = lastPage;
  copyNtagPagesToUserBuffer(NTAG_CC_PAGE, data, (uint8_t)(endPage - NTAG_CC_PAGE + 1u), lastPage, out, actualLen);
  if (hasCompleteNdefInBuffer(out, actualLen, endPage)) return true;

  for (uint16_t page = (uint16_t)(endPage + 1u); page <= lastPage;) {
    uint16_t thisEnd = (uint16_t)(page + NTAG_FAST_READ_PAGES - 1u);
    if (thisEnd > lastPage) thisEnd = lastPage;
    pageCount = 0;
    if (!pn532FastReadPages((uint8_t)page, (uint8_t)thisEnd, data, sizeof(data), pageCount)) {
      thisEnd = (uint16_t)(page + NTAG_INITIAL_FAST_READ_PAGES - 1u);
      if (thisEnd > lastPage) thisEnd = lastPage;
      pageCount = 0;
      if (!pn532FastReadPages((uint8_t)page, (uint8_t)thisEnd, data, sizeof(data), pageCount)) {
        return false;
      }
    }
    if (pageCount == 0) return false;
    copyNtagPagesToUserBuffer(page, data, pageCount, lastPage, out, actualLen);
    if (hasCompleteNdefInBuffer(out, actualLen, thisEnd)) return true;
    page = (uint16_t)(thisEnd + 1u);
  }
  (void)readStartMs;
  return true;
}

static bool readOpenSpoolUserAreaWindow(uint8_t* out, size_t outLen, size_t& actualLen) {
  if (outLen < NTAG_USER_BYTES) return false;
  memset(out, 0x00, outLen);
  actualLen = 0;
  uint16_t lastPage = 0;
  uint8_t data16[16] = {0};
  if (!pn532GetLastUserPage(lastPage, data16)) {
    debugPrint("[DEBUG] cannot determine user memory size");
    return false;
  }

  copyNtagPagesToUserBuffer(NTAG_CC_PAGE, data16, 4, lastPage, out, actualLen);
  uint16_t ccEndPage = (uint16_t)(NTAG_CC_PAGE + 3u);
  if (ccEndPage > lastPage) ccEndPage = lastPage;
  if (hasCompleteNdefInBuffer(out, actualLen, ccEndPage)) return true;

  for (uint16_t page = (uint16_t)(NTAG_FIRST_USER_PAGE + 3u); page <= lastPage; page += 4) {
    if (!pn532ReadPageWindow((uint8_t)page, data16)) {
      debugPrintf("[DEBUG] raw HSU read error at page=%u\n", (unsigned)page);
      return false;
    }

    uint16_t endPage = (uint16_t)(page + 3u);
    if (endPage > lastPage) endPage = lastPage;
    copyNtagPagesToUserBuffer(page, data16, (uint8_t)(endPage - page + 1u), lastPage, out, actualLen);
    if (hasCompleteNdefInBuffer(out, actualLen, endPage)) return true;
  }
  return true;
}

static bool readOpenSpoolUserArea(const uint8_t* uid, uint8_t uidLen, uint8_t* out, size_t outLen, size_t& actualLen) {
  if (readOpenSpoolUserAreaFast(out, outLen, actualLen)) return true;
  debugPrint("[DEBUG] NTAG FAST_READ unavailable, falling back to 4-page windows");
  if (reselectCurrentTagUid(uid, uidLen, PN532_TAG_DETECT_TIMEOUT_ACTIVE_MS)) {
    if (readOpenSpoolUserAreaFast(out, outLen, actualLen)) return true;
    debugPrint("[DEBUG] NTAG FAST_READ still unavailable after reselect");
  } else {
    debugPrint("[DEBUG] NTAG reselect before fallback failed");
  }
  return readOpenSpoolUserAreaWindow(out, outLen, actualLen);
}

static bool findNdefTlv(const uint8_t* buf, size_t len, size_t& ndefOffset, size_t& ndefLen) {
  size_t i = 0;
  while (i < len) {
    uint8_t t = buf[i];
    if (t == 0x00) { i++; continue; }
    if (t == 0xFE || i + 1 >= len) return false;

    if (t == 0x03) {
      uint8_t l = buf[i + 1];
      if (l == 0xFF) {
        if (i + 3 >= len) return false;
        ndefLen = ((size_t)buf[i + 2] << 8) | buf[i + 3];
        ndefOffset = i + 4;
      } else {
        ndefLen = l;
        ndefOffset = i + 2;
      }
      return (ndefOffset + ndefLen <= len);
    }

    uint8_t l = buf[i + 1];
    if (l == 0xFF) {
      if (i + 3 >= len) return false;
      size_t longLen = ((size_t)buf[i + 2] << 8) | buf[i + 3];
      i += 4 + longLen;
    } else {
      i += 2 + l;
    }
  }
  return false;
}

static bool parseMimeRecord(const uint8_t* ndef, size_t ndefLen, String& mime, String& payload) {
  if (ndefLen < 3) return false;

  uint8_t hdr = ndef[0];
  bool sr = hdr & 0x10;
  bool il = hdr & 0x08;
  uint8_t tnf = hdr & 0x07;
  if (tnf != 0x02) return false; // MIME media

  size_t p = 1;
  uint8_t typeLen = ndef[p++];

  uint32_t payloadLen = 0;
  if (sr) {
    if (p >= ndefLen) return false;
    payloadLen = ndef[p++];
  } else {
    if (p + 3 >= ndefLen) return false;
    payloadLen = ((uint32_t)ndef[p] << 24) | ((uint32_t)ndef[p + 1] << 16) | ((uint32_t)ndef[p + 2] << 8) | ndef[p + 3];
    p += 4;
  }

  uint8_t idLen = 0;
  if (il) {
    if (p >= ndefLen) return false;
    idLen = ndef[p++];
  }

  if (p + typeLen + idLen + payloadLen > ndefLen) return false;

  mime = "";
  payload = "";
  mime.reserve(typeLen);
  payload.reserve((unsigned int)payloadLen);
  for (uint8_t i = 0; i < typeLen; i++) mime += (char)ndef[p + i];
  p += typeLen + idLen;
  for (uint32_t i = 0; i < payloadLen; i++) payload += (char)ndef[p + i];
  return true;
}

static int parseHexColorToRgbInt(const char* colorHex) {
  if (!colorHex) return -1;
  String s = String(colorHex);
  s.trim();
  if (s.startsWith("#")) s.remove(0, 1);
  if (s.length() != 6) return -1;

  for (uint8_t i = 0; i < s.length(); i++) {
    char c = s[i];
    bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (!ok) return -1;
  }

  long rgb = strtol(s.c_str(), nullptr, 16);
  if (rgb < 0 || rgb > 0xFFFFFF) return -1;
  return (int)rgb;
}

static bool tryParseIntField(JsonVariantConst v, int& out) {
  if (v.is<int>()) { out = v.as<int>(); return true; }
  if (v.is<long>()) { out = (int)v.as<long>(); return true; }
  if (v.is<const char*>()) {
    String s = v.as<const char*>();
    s.trim();
    if (!s.length()) return false;
    out = s.toInt();
    return true;
  }
  return false;
}

static bool buildQidiTagJson(uint8_t materialId,
                             uint8_t colorId,
                             uint8_t vendorId,
                             String& outJson) {
  const QidiMaterialRecord* material = findQidiMaterial(materialId);
  String mainType = material ? String(material->type) : (String("QIDI-") + String(materialId));
  mainType.trim();
  if (!mainType.length()) mainType = String("QIDI-") + String(materialId);

  JsonDocument doc;
  doc["protocol"] = "openspool";
  doc["source"] = "qidi";
  doc["official"] = true;
  doc["brand"] = qidiVendorName(vendorId);
  doc["type"] = mainType;

  int rgb = qidiColorRgb(colorId);
  if (rgb >= 0) doc["color_hex"] = rgbIntToHexString(rgb);
  if (material && material->minTemp > 0) doc["min_temp"] = material->minTemp;
  if (material && material->maxTemp > 0) doc["max_temp"] = material->maxTemp;

  outJson = "";
  serializeJson(doc, outJson);
  return true;
}

static String bytesToCompactHex(const uint8_t* data, size_t len) {
  String out;
  out.reserve(len * 3);
  for (size_t i = 0; i < len; i++) {
    if (i > 0) out += ' ';
    if (data[i] < 0x10) out += '0';
    out += String(data[i], HEX);
  }
  out.toUpperCase();
  return out;
}

static bool readQidiFromCurrentTag(const uint8_t* uid, uint8_t uidLen, String& outJson) {
  if (uidLen != 4) return false;
  uint32_t readStartMs = millis();
  uint8_t reader = activeNfcReaderIndex;

  // Optional UID cache. Disabled by default because reprogrammed QIDI tags can
  // keep the same UID while the payload changes.
  if (qidiCacheLookup(uid, outJson)) {
    return true;
  }
  if (qidiPresenceCacheLookup(reader, uid, outJson)) {
    if (NFC_DIAG_SERIAL) {
      Serial.printf("[NFC %s] QIDI presence cache hit uid=%s\n",
                    READER_LABELS[activeNfcReaderIndex],
                    bytesToHexString(uid, uidLen).c_str());
    }
    return true;
  }

  if (lastQidiAuthFailKnown[reader] &&
      memcmp(lastQidiAuthFailUid[reader], uid, 4) == 0 &&
      (millis() - lastQidiAuthFailMs[reader]) < QIDI_AUTH_FAIL_COOLDOWN_MS) {
    return false;
  }

  if (!qidiAuthBlockOnce(uid, uidLen, QIDI_DATA_BLOCK)) {
    memcpy(lastQidiAuthFailUid[reader], uid, 4);
    lastQidiAuthFailKnown[reader] = true;
    lastQidiAuthFailMs[reader] = millis();
    pn532ResetAfterTagRead("qidi-auth");
    debugPrint("[DEBUG] QIDI auth failed");
    return false;
  }

  uint8_t data[16] = {0};
  if (!pn532MifareClassicReadBlock(QIDI_DATA_BLOCK, data)) {
    if (NFC_DIAG_SERIAL) {
      Serial.printf("[NFC %s] QIDI block read failed uid=%s\n",
                    READER_LABELS[activeNfcReaderIndex],
                    bytesToHexString(uid, uidLen).c_str());
    }
    pn532ResetAfterTagRead("qidi-read");
    debugPrint("[DEBUG] QIDI block read failed");
    return false;
  }
  if (false && NFC_DIAG_SERIAL) {
    Serial.printf("[NFC %s] QIDI raw block %u uid=%s data=%s\n",
                  READER_LABELS[activeNfcReaderIndex],
                  (unsigned)QIDI_DATA_BLOCK,
                  bytesToHexString(uid, uidLen).c_str(),
                  bytesToCompactHex(data, sizeof(data)).c_str());
  }

  bool ok = buildQidiTagJson(data[0], data[1], data[2], outJson);
  if (ok) {
    lastQidiAuthFailKnown[reader] = false;
    qidiCacheStore(uid, outJson);
    qidiPresenceCacheStore(reader, uid, outJson);
    if (NFC_DIAG_SERIAL) {
      Serial.printf("[NFC %s] QIDI read uid=%s material=%u color=%u vendor=%u time=%lu ms\n",
                    READER_LABELS[activeNfcReaderIndex],
                    bytesToHexString(uid, uidLen).c_str(),
                    (unsigned)data[0],
                    (unsigned)data[1],
                    (unsigned)data[2],
                    (unsigned long)(millis() - readStartMs));
    }
    debugPrintf("[DEBUG] QIDI tag mapped material=%u color=%u vendor=%u json=%s\n",
                data[0], data[1], data[2], outJson.c_str());
    pn532ResetAfterTagRead("qidi");
  }
  return ok;
}

static bool parseOpenSpoolFields(const String& openspoolJson, OpenSpoolFields& fields) {
  fields = OpenSpoolFields();

  JsonDocument src;
  DeserializationError err = deserializeJson(src, openspoolJson);
  if (err) return false;

  const char* protocol = src["protocol"].as<const char*>();
  if (!protocol || strcmp(protocol, "openspool") != 0) return false;

  fields.vendor = src["brand"].is<const char*>() ? String(src["brand"].as<const char*>()) : "";
  fields.mainType = src["type"].is<const char*>() ? String(src["type"].as<const char*>()) : "";
  fields.subType = src["subtype"].is<const char*>() ? String(src["subtype"].as<const char*>()) : "";
  if (src["color_hex"].is<const char*>()) {
    int rgb = parseHexColorToRgbInt(src["color_hex"].as<const char*>());
    if (rgb >= 0) fields.colorHex = rgbIntToHexString(rgb);
  }

  int iv = 0;
  if (tryParseIntField(src["alpha"], iv)) fields.alpha = constrain(iv, 0, 255);
  if (tryParseIntField(src["min_temp"], iv)) fields.minTemp = iv;
  if (tryParseIntField(src["max_temp"], iv)) fields.maxTemp = iv;
  if (tryParseIntField(src["bed_min_temp"], iv)) {
    fields.bedTemp = iv;
  } else if (tryParseIntField(src["bed_max_temp"], iv)) {
    fields.bedTemp = iv;
  }
  if (src["official"].is<bool>()) {
    fields.officialKnown = true;
    fields.official = src["official"].as<bool>();
  }

  return true;
}

static bool buildDesiredPrinterComparableFingerprint(const OpenSpoolFields& fields,
                                                     const uint8_t* uid,
                                                     uint8_t uidLen,
                                                     String& outFingerprint,
                                                     bool includeUid) {
  outFingerprint = "";
  outFingerprint.reserve(220);
  outFingerprint += String(gSettings.channels[activeReaderIndex]);
  outFingerprint += '|';
  outFingerprint += fields.vendor;
  outFingerprint += '|';
  outFingerprint += fields.mainType;
  outFingerprint += '|';
  outFingerprint += fields.subType;
  outFingerprint += '|';
  outFingerprint += fields.colorHex;
  outFingerprint += '|';
  if (includeUid) outFingerprint += uidBytesToCsv(uid, uidLen);
  outFingerprint += '|';
  outFingerprint += String(fields.minTemp);
  outFingerprint += '|';
  outFingerprint += String(fields.maxTemp);
  outFingerprint += '|';
  outFingerprint += String(fields.bedTemp);
  return true;
}

static String currentPrinterComparableFingerprint() {
  String fp;
  fp.reserve(220);
  fp += String(gSettings.channels[activeReaderIndex]);
  fp += '|';
  fp += gPrinterState.vendor;
  fp += '|';
  fp += gPrinterState.mainType;
  fp += '|';
  fp += gPrinterState.subType;
  fp += '|';
  fp += gPrinterState.colorHex;
  fp += '|';
  fp += gPrinterState.cardUidCsv;
  fp += '|';
  fp += String(gPrinterState.minTemp);
  fp += '|';
  fp += String(gPrinterState.maxTemp);
  fp += '|';
  fp += String(gPrinterState.bedTemp);
  return fp;
}

static bool buildFilamentDetectPayload(const OpenSpoolFields& fields,
                                       const uint8_t* uid,
                                       uint8_t uidLen,
                                       String& outPayload,
                                       String& outFingerprint) {
  JsonDocument req;
  req["channel"] = gSettings.channels[activeReaderIndex];
  JsonObject info = req["info"].to<JsonObject>();

  if (fields.vendor.length()) info["VENDOR"] = fields.vendor;
  if (fields.mainType.length()) info["MAIN_TYPE"] = fields.mainType;
  if (fields.subType.length()) info["SUB_TYPE"] = fields.subType;
  if (fields.colorHex.length()) {
    int rgb = parseHexColorToRgbInt(fields.colorHex.c_str());
    if (rgb >= 0) info["RGB_1"] = rgb;
  }
  if (fields.alpha >= 0) info["ALPHA"] = fields.alpha;
  if (fields.minTemp >= 0) info["HOTEND_MIN_TEMP"] = fields.minTemp;
  if (fields.maxTemp >= 0) info["HOTEND_MAX_TEMP"] = fields.maxTemp;
  // OFFICIAL is derived by the printer firmware and is not accepted by filament_detect/set.
  if (fields.bedTemp >= 0) info["BED_TEMP"] = fields.bedTemp;

  JsonArray uidArr = info["CARD_UID"].to<JsonArray>();
  for (uint8_t i = 0; i < uidLen; i++) uidArr.add((int)uid[i]);

  outPayload = "";
  serializeJson(req, outPayload);
  outFingerprint = outPayload;

  return true;
}

// ------------------------------ Printer API ------------------------------
static bool postJson(const String& url, const String& payload, int& httpCode, String& resp) {
  if (WiFi.status() != WL_CONNECTED) return false;

  debugPrintf("[DEBUG] WEBHOOK POST %s\n", url.c_str());
  debugPrintf("[DEBUG] WEBHOOK PAYLOAD %s\n", payload.c_str());

  HTTPClient http;
  http.setTimeout(2500);
  if (!http.begin(url)) return false;

  http.addHeader("Content-Type", "application/json");
  httpCode = http.POST((uint8_t*)payload.c_str(), payload.length());
  resp = (httpCode > 0) ? http.getString() : "";
  http.end();

  debugPrintf("[DEBUG] WEBHOOK RESULT ok=%d code=%d body=%s\n",
              httpCode > 0 ? 1 : 0,
              httpCode,
              resp.c_str());

  return httpCode > 0;
}

static bool recordWebhookResult(bool transportOk, int code, const String& resp) {
  bool hookOk = transportOk && code >= 200 && code < 300;
  bool hookChanged = !gWebhookState.known ||
                     gWebhookState.ok != hookOk ||
                     gWebhookState.httpCode != code ||
                     gWebhookState.response != resp;
  gWebhookState.known = true;
  gWebhookState.ok = hookOk;
  gWebhookState.httpCode = code;
  gWebhookState.lastSentMs = millis();
  gWebhookState.response = resp;
  if (hookChanged) bumpStateRevision();
  return hookOk;
}

static bool sendFilamentSetPayload(const String& payload, const String& fingerprint, const char* reason, bool pulseOnFail) {
  int code = 0;
  String resp;
  bool ok = postJson(filamentDetectUrl(), payload, code, resp);
  bool hookOk = recordWebhookResult(ok, code, resp);
  Serial.printf("[API] SET sent=%d code=%d reason=%s payload=%s\n",
                ok ? 1 : 0,
                code,
                reason ? reason : "tag",
                payload.c_str());

  if (hookOk) {
    lastSentFingerprint[activeReaderIndex] = fingerprint;
  } else if (pulseOnFail) {
    pulseTagLedError();
  }
  return hookOk;
}

static void scheduleSetVerification(const String& payload,
                                    const String& fingerprint,
                                    const String& expectedWithUid,
                                    const String& expectedNoUid) {
  pendingVerifyPayload[activeReaderIndex] = payload;
  pendingVerifyFingerprint[activeReaderIndex] = fingerprint;
  pendingVerifyExpectedWithUid[activeReaderIndex] = expectedWithUid;
  pendingVerifyExpectedNoUid[activeReaderIndex] = expectedNoUid;
  pendingVerifyRetriesLeft[activeReaderIndex] = WEBHOOK_VERIFY_MAX_RETRIES;
  pendingVerifyDueMs[activeReaderIndex] = millis() + WEBHOOK_VERIFY_DELAY_MS;
  debugPrintf("[DEBUG] SET verification scheduled in %lu ms\n", (unsigned long)WEBHOOK_VERIFY_DELAY_MS);
}

static void processPendingSetVerification() {
  if (pendingVerifyDueMs[activeReaderIndex] == 0 || portalMode || WiFi.status() != WL_CONNECTED) return;
  uint32_t now = millis();
  if ((int32_t)(now - pendingVerifyDueMs[activeReaderIndex]) < 0) return;

  if (fetchPrinterInfoState("set verify") && currentPrinterMatchesExpected(pendingVerifyExpectedWithUid[activeReaderIndex], pendingVerifyExpectedNoUid[activeReaderIndex])) {
    debugPrint("[DEBUG] SET verified on printer");
    clearPendingVerification();
    scheduleNextInfoSync(millis());
    return;
  }

  if (pendingVerifyRetriesLeft[activeReaderIndex] == 0) {
    debugPrint("[DEBUG] SET verification failed, retries exhausted");
    pulseTagLedError();
    clearPendingVerification();
    return;
  }

  if (!allowTagSetWithFreshFilamentState()) {
    debugPrint("[DEBUG] SET retry cancelled because filament is loaded or sensor state is unavailable");
    clearPendingVerification();
    return;
  }

  pendingVerifyRetriesLeft[activeReaderIndex]--;
  debugPrintf("[DEBUG] SET verification mismatch, retrying payload, retries_left=%u\n", pendingVerifyRetriesLeft[activeReaderIndex]);
  if (sendFilamentSetPayload(pendingVerifyPayload[activeReaderIndex], pendingVerifyFingerprint[activeReaderIndex], "verify retry", false)) {
    pendingVerifyDueMs[activeReaderIndex] = millis() + WEBHOOK_VERIFY_RETRY_MS;
  } else if (pendingVerifyRetriesLeft[activeReaderIndex] == 0) {
    pulseTagLedError();
    clearPendingVerification();
  } else {
    pendingVerifyDueMs[activeReaderIndex] = millis() + WEBHOOK_VERIFY_RETRY_MS;
  }
}

static void handleValidMappedTagPayload(const char* source, const String& openspoolJson, const uint8_t* uid, uint8_t uidLen) {
  OpenSpoolFields fields;
  if (!parseOpenSpoolFields(openspoolJson, fields)) {
    Serial.printf("[NFC] %s parse failed\n", source ? source : "tag");
    debugPrintf("[DEBUG] NFC OpenSpool parse failed for UID=%s\n", bytesToHexString(uid, uidLen).c_str());
    pulseTagLedError();
    return;
  }

  String payload;
  String fingerprint;
  if (!buildFilamentDetectPayload(fields, uid, uidLen, payload, fingerprint)) {
    Serial.printf("[NFC] %s parse failed\n", source ? source : "tag");
    debugPrintf("[DEBUG] NFC payload parse failed for UID=%s\n", bytesToHexString(uid, uidLen).c_str());
    pulseTagLedError();
    return;
  }

  lastTagSeenMs[activeReaderIndex] = millis();
  storeTagState(fields, openspoolJson, uid, uidLen, payload, fingerprint, source);
  Serial.printf("[NFC %s] tag recognized\n", READER_LABELS[activeReaderIndex]);
  lastAcceptedUidLen[activeReaderIndex] = uidLen > sizeof(lastAcceptedUid[activeReaderIndex]) ? sizeof(lastAcceptedUid[activeReaderIndex]) : uidLen;
  memcpy(lastAcceptedUid[activeReaderIndex], uid, lastAcceptedUidLen[activeReaderIndex]);
  lastAcceptedUidMs[activeReaderIndex] = millis();

  if (fingerprint != lastObservedFingerprint[activeReaderIndex]) {
    lastObservedFingerprint[activeReaderIndex] = fingerprint;
    debugPrintf("[DEBUG] NFC UID %s\n", bytesToHexString(uid, uidLen).c_str());
    debugPrintf("[DEBUG] NFC %s JSON %s\n", source ? source : "tag", openspoolJson.c_str());
    debugPrintf("[DEBUG] NFC mapped payload %s\n", payload.c_str());
  }

  String expectedWithUid;
  String expectedNoUid;
  bool comparableOk = buildDesiredPrinterComparableFingerprint(fields, uid, uidLen, expectedWithUid, true) &&
                      buildDesiredPrinterComparableFingerprint(fields, uid, uidLen, expectedNoUid, false);

  bool sentBefore = fingerprint == lastSentFingerprint[activeReaderIndex];
  bool printerInfoFresh = gPrinterState.queryOk &&
                          gPrinterState.hasInfo &&
                          gPrinterState.lastQueryMs != 0 &&
                          (millis() - gPrinterState.lastQueryMs) <= PRINTER_TAG_INFO_MAX_AGE_MS;
  bool printerMatches = printerInfoFresh && comparableOk && currentPrinterMatchesExpected(expectedWithUid, expectedNoUid);
  if (printerMatches) {
    lastSentFingerprint[activeReaderIndex] = fingerprint;
    clearPendingVerification();
    debugPrint("[DEBUG] Printer already matches tag, webhook skipped");
    return;
  }

  // Never change filament metadata after filament has reached the Tool Head.
  // Refresh the sensor immediately before SET so a recent load cannot race an RFID read.
  if (!allowTagSetWithFreshFilamentState()) {
    return;
  }

  if (printerInfoFresh && comparableOk) {
    debugPrintf("[DEBUG] Printer channel differs from tag, SET allowed. desired=%s current=%s\n",
                expectedWithUid.c_str(),
                currentPrinterComparableFingerprint().c_str());
  }

  if (sentBefore && gWebhookState.lastSentMs && (millis() - gWebhookState.lastSentMs) < WEBHOOK_RESEND_GRACE_MS) {
    debugPrint("[DEBUG] Payload recently sent, waiting for printer confirmation");
    return;
  }
  if (sentBefore) {
    debugPrint("[DEBUG] Payload unchanged locally but printer differs, webhook resend forced");
  }

  if (sendFilamentSetPayload(payload, fingerprint, "tag", true) && comparableOk) {
    scheduleSetVerification(payload, fingerprint, expectedWithUid, expectedNoUid);
  }
}

// ------------------------------ Tag polling ------------------------------
static bool detectCurrentTagUid(uint8_t* uid, uint8_t& uidLen, uint16_t detectTimeoutMs) {
  uidLen = 0;
  if (!pn532InListPassiveTarget(uid, uidLen, detectTimeoutMs)) {
    if (NFC_DIAG_SERIAL && (lastNfcProbeLogMs[activeNfcReaderIndex] == 0 || (millis() - lastNfcProbeLogMs[activeNfcReaderIndex]) >= NFC_NO_TAG_LOG_MS)) {
      lastNfcProbeLogMs[activeNfcReaderIndex] = millis();
      Serial.printf("[NFC %s] polling, no tag detected\n", READER_LABELS[activeNfcReaderIndex]);
    }
    return false;
  }

  pulseTagLed();
  debugPrintf("[DEBUG] NFC tag detected, UID=%s\n", bytesToHexString(uid, uidLen).c_str());

  // Briefly focus this reader while the tag is being handled.
  const bool wasUnlocked = (nfcFocusReader < 0);
  const bool switchedReader = (nfcFocusReader >= 0 && nfcFocusReader != (int8_t)activeNfcReaderIndex);
  nfcFocusReader  = activeNfcReaderIndex;
  nfcFocusUntilMs = millis() + TAG_DETECT_FOCUS_MS;
  if ((wasUnlocked || switchedReader) && NFC_DIAG_SERIAL && uidLen != 4) {
    Serial.printf("[NFC] reader lock acquired: %s locked for %lu ms\n",
                  READER_LABELS[activeNfcReaderIndex],
                  (unsigned long)TAG_DETECT_FOCUS_MS);
  }
  return true;
}

static bool shouldSkipRecentTagContent(const uint8_t* uid, uint8_t uidLen) {
  if (TAG_SAME_UID_SKIP_MS == 0) return false;
  if (lastAcceptedUidLen[activeNfcReaderIndex] != uidLen) return false;
  if (lastAcceptedUidMs[activeNfcReaderIndex] == 0) return false;
  if ((millis() - lastAcceptedUidMs[activeNfcReaderIndex]) > TAG_SAME_UID_SKIP_MS) return false;
  if (memcmp(lastAcceptedUid[activeNfcReaderIndex], uid, uidLen) != 0) return false;
  return true;
}

static bool reselectCurrentTagUid(const uint8_t* uid, uint8_t uidLen, uint16_t timeoutMs) {
  if (!uid || uidLen == 0 || uidLen > 10) return false;
  pn532ReleaseTargets();
  delay(TAG_READ_SETTLE_MS);

  uint8_t foundUid[10] = {0};
  uint8_t foundLen = 0;
  if (!pn532InListPassiveTarget(foundUid, foundLen, timeoutMs)) return false;
  if (foundLen != uidLen) return false;
  return memcmp(foundUid, uid, uidLen) == 0;
}

static void releaseReaderFocusAfterReadAttempt() {
  nfcFocusReader = -1;
  nfcFocusUntilMs = 0;
}

static bool readOpenSpoolFromDetectedTag(const uint8_t* uid, uint8_t uidLen, String& outJson) {
  if (uidLen == 4) return false;
  uint32_t readStartMs = millis();
  delay(TAG_READ_SETTLE_MS);

  static uint8_t buf[NTAG_USER_BYTES];
  size_t bufLen = 0;
  if (!readOpenSpoolUserArea(uid, uidLen, buf, sizeof(buf), bufLen)) {
    if (NFC_DIAG_SERIAL) {
      Serial.printf("[NFC %s] OpenSpool/NTAG user area read failed uid=%s\n",
                    READER_LABELS[activeNfcReaderIndex],
                    bytesToHexString(uid, uidLen).c_str());
    }
    debugPrint("[DEBUG] NTAG user area read failed");
    if (uidLen != 4) pulseTagLedError();
    return false;
  }

  size_t ndefOffset = 0, ndefLen = 0;
  if (!findNdefTlv(buf, bufLen, ndefOffset, ndefLen)) {
    if (NFC_DIAG_SERIAL) {
      Serial.printf("[NFC %s] OpenSpool NDEF TLV not found uid=%s raw=%u bytes\n",
                    READER_LABELS[activeNfcReaderIndex],
                    bytesToHexString(uid, uidLen).c_str(),
                    (unsigned)bufLen);
    }
    debugPrint("[DEBUG] No NDEF TLV found");
    if (uidLen != 4) pulseTagLedError();
    return false;
  }

  String mime;
  outJson = "";
  if (!parseMimeRecord(buf + ndefOffset, ndefLen, mime, outJson)) {
    if (NFC_DIAG_SERIAL) {
      Serial.printf("[NFC %s] OpenSpool MIME parse failed uid=%s ndef=%u bytes\n",
                    READER_LABELS[activeNfcReaderIndex],
                    bytesToHexString(uid, uidLen).c_str(),
                    (unsigned)ndefLen);
    }
    debugPrint("[DEBUG] Failed to parse NDEF MIME record");
    if (uidLen != 4) pulseTagLedError();
    return false;
  }
  debugPrintf("[DEBUG] NDEF MIME=%s length=%u\n", mime.c_str(), (unsigned)outJson.length());
  if (mime != "application/json") {
    if (NFC_DIAG_SERIAL) {
      Serial.printf("[NFC %s] ignored tag MIME=%s uid=%s\n",
                    READER_LABELS[activeNfcReaderIndex],
                    mime.c_str(),
                    bytesToHexString(uid, uidLen).c_str());
    }
    debugPrint("[DEBUG] Ignored tag because MIME is not application/json");
    if (uidLen != 4) pulseTagLedError();
    return false;
  }

  // Avoid parsing extended OpenSpool JSON twice; the mapping step validates protocol strictly.
  debugPrintf("[DEBUG] NFC JSON read completed in %lu ms, raw=%u bytes, payload=%u bytes\n",
              (unsigned long)(millis() - readStartMs),
              (unsigned)bufLen,
              (unsigned)outJson.length());
  if (NFC_DIAG_SERIAL) {
    Serial.printf("[NFC %s] OpenSpool read uid=%s time=%lu ms raw=%u payload=%u bytes\n",
                  READER_LABELS[activeNfcReaderIndex],
                  bytesToHexString(uid, uidLen).c_str(),
                  (unsigned long)(millis() - readStartMs),
                  (unsigned)bufLen,
                  (unsigned)outJson.length());
  }
  return true;
}

static void processPrinterPolling() {
  if (portalMode || WiFi.status() != WL_CONNECTED) return;

  uint32_t now = millis();
  if (nextMotionQueryMs[activeReaderIndex] == 0 || nextInfoSyncMs[activeReaderIndex] == 0) {
    scheduleInitialPrinterQueries(now);
  }

  if ((int32_t)(now - nextMotionQueryMs[activeReaderIndex]) >= 0) {
    fetchPrinterMotionState();
    now = millis();
    scheduleNextMotionQuery(now);
  }

  now = millis();
  if ((int32_t)(now - nextInfoSyncMs[activeReaderIndex]) >= 0) {
    fetchPrinterInfoState("slow sync");
    scheduleNextInfoSync(millis());
  }
}

static void updateFeederActivityTransitions() {
  for (uint8_t reader = 0; reader < READER_COUNT; reader++) {
    if (!gPrinterStates[reader].queryOk || !gPrinterStates[reader].motionKnown) continue;

    bool current = gPrinterStates[reader].filamentDetected;
    if (!feederMotionBaselineKnown[reader]) {
      feederMotionBaselineKnown[reader] = true;
      lastFeederMotionValue[reader] = current;
      if (NFC_DIAG_SERIAL) {
        Serial.printf("[FEEDER %s] baseline filament_detected=%d\n", READER_LABELS[reader], current ? 1 : 0);
      }
      continue;
    }

    if (current && !lastFeederMotionValue[reader]) {
      lastFeederMotionRiseMs[reader] = millis();
      if (NFC_DIAG_SERIAL) {
        Serial.printf("[FEEDER %s] filament detected, NFC polling paused while loaded\n",
                      READER_LABELS[reader]);
      }
    }
    lastFeederMotionValue[reader] = current;
  }
}

static int8_t activeFeederReader() {
  int8_t active = -1;
  uint32_t newest = 0;
  uint32_t now = millis();
  for (uint8_t reader = 0; reader < READER_COUNT; reader++) {
    if (lastFeederMotionRiseMs[reader] == 0) continue;
    if ((now - lastFeederMotionRiseMs[reader]) > FEEDER_PRIORITY_WINDOW_MS) continue;
    if (filamentSensorLocksReader(reader)) continue;
    if (active < 0 || lastFeederMotionRiseMs[reader] > newest) {
      active = (int8_t)reader;
      newest = lastFeederMotionRiseMs[reader];
    }
  }
  return active;
}

static bool feederPriorityHasEverTriggered() {
  for (uint8_t reader = 0; reader < READER_COUNT; reader++) {
    if (lastFeederMotionRiseMs[reader] != 0) return true;
  }
  return false;
}

static void enqueueNfcTag(uint8_t reader, const char* source, const String& json, const uint8_t* uid, uint8_t uidLen) {
  if (!nfcTagQueue || !source || !uid || uidLen == 0) return;

  NfcQueueItem item;
  item.reader = reader < READER_COUNT ? reader : 0;
  item.uidLen = uidLen > sizeof(item.uid) ? sizeof(item.uid) : uidLen;
  memcpy(item.uid, uid, item.uidLen);
  safeCopy(item.source, sizeof(item.source), source);
  safeCopy(item.json, sizeof(item.json), json);

  if (xQueueSend(nfcTagQueue, &item, 0) != pdTRUE && NFC_DIAG_SERIAL) {
    Serial.printf("[NFC %s] tag queue full, dropping %s payload\n", READER_LABELS[item.reader], item.source);
  }
}

static void processQueuedNfcTags() {
  if (!nfcTagQueue) return;

  NfcQueueItem item;
  while (xQueueReceive(nfcTagQueue, &item, 0) == pdTRUE) {
    if (item.reader >= READER_COUNT || item.uidLen == 0) continue;
    selectReader(item.reader);
    handleValidMappedTagPayload(item.source, String(item.json), item.uid, item.uidLen);
  }
}

static void nfcPollingTask(void* /*pvParam*/) {
  if (NFC_DIAG_SERIAL) {
    Serial.printf("[NFC] polling task started on core %d\n", xPortGetCoreID());
  }

  for (;;) {
    uint32_t now = millis();
    updateFeederActivityTransitions();
    int8_t feederReader = activeFeederReader();
    const bool benchPollMode = feederReader < 0 && !feederPriorityHasEverTriggered();

    for (uint8_t reader = 0; reader < READER_COUNT; reader++) {
      if (filamentSensorLocksReader(reader)) {
        if (nfcFocusReader == (int8_t)reader) {
          releaseReaderFocusAfterReadAttempt();
        }
        continue;
      }
      selectNfcReader(reader);

      const bool feederActive = feederReader == (int8_t)reader;
      if (feederReader >= 0 && !feederActive) continue;

      if (nfcFocusReader >= 0 && (int32_t)(now - nfcFocusUntilMs) < 0 && nfcFocusReader != (int8_t)reader) continue;
      if (nfcFocusReader >= 0 && (int32_t)(now - nfcFocusUntilMs) >= 0) {
        if (NFC_DIAG_SERIAL) {
          Serial.printf("[NFC] reader lock expired after %lu ms, resuming dual-scan\n",
                        (unsigned long)TAG_DETECT_FOCUS_MS);
        }
        nfcFocusReader = -1;
      }

      const bool focusedReader = nfcFocusReader == (int8_t)reader && (int32_t)(now - nfcFocusUntilMs) < 0;
      const uint32_t pollInterval = (feederActive || focusedReader) ? TAG_ACTIVE_POLL_MS : (benchPollMode ? TAG_BENCH_POLL_MS : TAG_IDLE_POLL_MS);
      const uint16_t detectTimeout = (feederActive || focusedReader) ? PN532_TAG_DETECT_TIMEOUT_ACTIVE_MS : (benchPollMode ? PN532_TAG_DETECT_TIMEOUT_BENCH_MS : PN532_TAG_DETECT_TIMEOUT_IDLE_MS);

      if (!nfcReady[reader]) continue;
      now = millis();
      if ((now - lastPollMs[reader]) < pollInterval) continue;
      lastPollMs[reader] = now;
      prepareReaderRfField(reader);

      uint8_t uid[10] = {0};
      uint8_t uidLen = 0;

      if (!detectCurrentTagUid(uid, uidLen, detectTimeout)) {
        qidiPresenceCacheNoteNoTag(reader);
        continue;
      }
      if (shouldSkipRecentTagContent(uid, uidLen)) continue;

      if (uidLen == 4) {
        String qidiJson;
        bool hasQidiTag = readQidiFromCurrentTag(uid, uidLen, qidiJson);
        releaseReaderFocusAfterReadAttempt();
        if (NFC_DIAG_SERIAL) {
          Serial.printf("[NFC %s] mode=QIDI/MIFARE Classic uid=%s result=%s\n",
                        READER_LABELS[reader],
                        bytesToHexString(uid, uidLen).c_str(),
                        hasQidiTag ? "ok" : "failed");
        }
        if (hasQidiTag) {
          enqueueNfcTag(reader, "QIDI", qidiJson, uid, uidLen);
        }
      } else {
        if (NFC_DIAG_SERIAL) {
          Serial.printf("[NFC %s] mode=OpenSpool/NTAG uid=%s\n",
                        READER_LABELS[reader],
                        bytesToHexString(uid, uidLen).c_str());
        }
        String openspoolJson;
        bool hasValidTag = readOpenSpoolFromDetectedTag(uid, uidLen, openspoolJson);
        pn532ResetAfterTagRead("openspool");
        releaseReaderFocusAfterReadAttempt();
        if (hasValidTag) {
          enqueueNfcTag(reader, "OpenSpool", openspoolJson, uid, uidLen);
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ============================== Setup / Loop ==============================
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(400);
  randomSeed((uint32_t)ESP.getEfuseMac() ^ micros());
  Serial.printf("\n[%s] boot %s\n", FW_NAME, FW_VERSION);
  Serial.printf("[SERIAL] monitor ready at %lu baud\n", (unsigned long)SERIAL_BAUD);
  if (isDebugSerialEnabled()) {
    Serial.printf("[BOARD] %s, dual PN532 HSU/UART mode\n", BOARD_LABEL);
    for (uint8_t reader = 0; reader < READER_COUNT; reader++) {
      Serial.printf("[PN532 %s] wiring RX=GPIO%d TX=GPIO%d reset=%d baud=%lu\n",
                    READER_LABELS[reader],
                    PN532_RX_PINS[reader],
                    PN532_TX_PINS[reader],
                    PN532_RESET_PINS[reader],
                    PN532_BAUD);
    }
  }
  debugPrintf("[DEBUG] Tag activity LED pin=%d active=%s\n",
              TAG_LED_PIN,
              TAG_LED_ACTIVE_HIGH ? "HIGH" : "LOW");

  if (TAG_LED_PIN >= 0) {
    pinMode(TAG_LED_PIN, OUTPUT);
    setTagLed(false);
  }

  loadSettings();
  loadQidiConfig();
  for (uint8_t reader = 0; reader < READER_COUNT; reader++) {
    selectReader(reader);
    gPrinterState.endpoint = printerChannelQueryUrl();
  }
  selectReader(0);
  if (isDebugSerialEnabled()) {
    Serial.printf("[CONFIG] SSID=%s, pass=%u chars, host=%s%s, printer=%s:%u (%s), left_channel=%u, left_tool_head=%u, right_channel=%u, right_tool_head=%u, setup_ap=%s, debug_serial=%u\n",
                  strlen(gSettings.wifiSsid) ? gSettings.wifiSsid : "(empty)",
                  (unsigned)strlen(gSettings.wifiPass),
                  strlen(gSettings.hostname) ? gSettings.hostname : "(empty)",
                  strlen(gSettings.hostname) ? ".local" : "",
                  strlen(gSettings.printerIp) ? gSettings.printerIp : "(empty)",
                  (unsigned)gSettings.printerPort,
                  printerAddressType(String(gSettings.printerIp)),
                  (unsigned)gSettings.channels[0],
                  (unsigned)(gSettings.channels[0] + 1),
                  (unsigned)gSettings.channels[1],
                  (unsigned)(gSettings.channels[1] + 1),
                  setupApSsid(),
                  gSettings.debugSerial ? 1 : 0);
    Serial.printf("[QIDI] cfg=%s, materials=%u, vendors=%u\n",
                  gQidiCustomConfig ? "custom" : "built-in",
                  (unsigned)(gQidiCustomConfig ? gQidiMaterialCount : (sizeof(DEFAULT_QIDI_MATERIALS) / sizeof(DEFAULT_QIDI_MATERIALS[0]))),
                  (unsigned)(gQidiCustomConfig ? gQidiVendorCount : (sizeof(DEFAULT_QIDI_VENDORS) / sizeof(DEFAULT_QIDI_VENDORS[0]))));
  }

  bool staOk = false;
  if (strlen(gSettings.wifiSsid) > 0) {
    staOk = connectSta();
  }
  if (!staOk && !portalMode) {
    startPortal();
  }

  for (uint8_t reader = 0; reader < READER_COUNT; reader++) {
    selectNfcReader(reader);
    if (isDebugSerialEnabled()) {
      Serial.printf("[PN532 %s] starting UART on RX=GPIO%d TX=GPIO%d\n",
                    READER_LABELS[reader],
                    PN532_RX_PINS[reader],
                    PN532_TX_PINS[reader]);
    }
    PN532Serial.begin(PN532_BAUD, SERIAL_8N1, PN532_RX_PINS[reader], PN532_TX_PINS[reader]);

    if (!nfc.begin()) {
      nfcReady[reader] = false;
      Serial.printf("[PN532 %s] begin failed\n", READER_LABELS[reader]);
      debugPrint("[DEBUG] PN532 begin failed - check HSU mode, power and RX/TX wiring");
    } else {
      uint32_t version = nfc.getFirmwareVersion();
      if (!version) {
        nfcReady[reader] = false;
        Serial.printf("[PN532 %s] not found\n", READER_LABELS[reader]);
        debugPrint("[DEBUG] PN532 firmware read failed");
      } else {
        bool samOk = nfc.SAMConfig();
        bool retryOk = nfc.setPassiveActivationRetries(PN532_PASSIVE_RETRIES);
        bool rfOnOk = pn532SetRfField(true);
        nfcReady[reader] = true;
        if (isDebugSerialEnabled()) {
          Serial.printf("[PN532 %s] ready\n", READER_LABELS[reader]);
          Serial.printf("[PN532 %s] firmware IC=0x%02lX ver=%lu rev=%lu support=0x%02lX\n",
                        READER_LABELS[reader],
                        (version >> 24) & 0xFF,
                        (version >> 16) & 0xFF,
                        (version >> 8) & 0xFF,
                        version & 0xFF);
          Serial.printf("[PN532 %s] SAMConfig=%s passive_retries=0x%02X(%s) RF field on=%s\n",
                        READER_LABELS[reader],
                        samOk ? "ok" : "failed",
                        PN532_PASSIVE_RETRIES,
                        retryOk ? "ok" : "failed",
                        rfOnOk ? "ok" : "failed");
        }
        if (PN532_SWITCH_RF_FIELD_BETWEEN_READERS) {
          bool rfOffOk = pn532SetRfField(false);
          if (isDebugSerialEnabled()) {
            Serial.printf("[PN532 %s] initial RF field off %s\n", READER_LABELS[reader], rfOffOk ? "ok" : "failed");
          }
        }
      }
    }
  }
  if (isDebugSerialEnabled()) {
    Serial.printf("[PN532] summary: left=%s, right=%s\n",
                  nfcReady[0] ? "ready" : "not found",
                  nfcReady[1] ? "ready" : "not found");
  }
  selectNfcReader(0);

  for (uint8_t reader = 0; reader < READER_COUNT; reader++) {
    selectReader(reader);
    lastPollMs[reader] = millis();
    lastTagSeenMs[activeReaderIndex] = 0;
    scheduleInitialPrinterQueries(millis());
  }
  selectReader(0);

  nfcTagQueue = xQueueCreate(NFC_QUEUE_DEPTH, sizeof(NfcQueueItem));
  if (!nfcTagQueue) {
    Serial.println("[NFC] tag queue allocation failed");
  } else {
    BaseType_t taskOk = xTaskCreatePinnedToCore(
      nfcPollingTask,
      "nfc-poll",
      NFC_TASK_STACK_BYTES,
      nullptr,
      2,
      &nfcTaskHandle,
      0);
    if (isDebugSerialEnabled()) {
      Serial.printf("[NFC] polling task create %s, queue_depth=%u, stack=%lu bytes\n",
                    taskOk == pdPASS ? "ok" : "failed",
                    (unsigned)NFC_QUEUE_DEPTH,
                    (unsigned long)NFC_TASK_STACK_BYTES);
    }
  }
}

void loop() {
  uint32_t now = millis();

  if (tagLedUntilMs != 0 && (int32_t)(now - tagLedUntilMs) >= 0) {
    setTagLed(false);
    tagLedUntilMs = 0;
  }

  if (portalMode) {
    dnsServer.processNextRequest();
  }
  web.handleClient();
  maintainWifiConnection();
  maintainVisibleWifiBssidScan();
  processQueuedNfcTags();
  now = millis();

  for (uint8_t reader = 0; reader < READER_COUNT; reader++) {
    selectReader(reader);
    if (gTagState.active && gTagState.lastSeenMs != 0 && (now - gTagState.lastSeenMs) > TAG_ACTIVE_WINDOW_MS) {
      gTagState.active = false;
      bumpStateRevision();
    }
    processPendingSetVerification();
    processPrinterPolling();
  }
  selectReader(0);
}
