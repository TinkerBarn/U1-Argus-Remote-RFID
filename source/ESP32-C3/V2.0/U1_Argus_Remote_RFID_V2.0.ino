/*
  U1 Argus Remote RFID - V2.0

  Target: ESP32-C3 Super Mini + PN532 (HSU/UART)

  Required libraries:
  - Adafruit PN532
  - ArduinoJson

  Wiring for ESP32-C3 Super Mini -> PN532 in HSU/UART mode:
  - 3V3       -> VCC
  - GND       -> GND
  - GPIO3 TX  -> PN532 pin labeled SCL
  - GPIO4 RX  -> PN532 pin labeled SDA
*/

#include <WiFi.h>
#include <stdarg.h>
#include <string.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_PN532.h>

// ============================== Version ==============================
static const char* FW_NAME = "U1 Argus Remote RFID";
static const char* FW_VERSION = "V2.0";

// ============================== Debug ==============================
static const uint32_t SERIAL_BAUD = 115200;
// Keep standard Serial logs. Set to 1 only while diagnosing raw NFC/API payloads.
#define VERBOSE_DEBUG 0
static const int TAG_LED_PIN = 8;
static const bool TAG_LED_ACTIVE_HIGH = true;
static const uint32_t TAG_LED_HOLD_MS = 300;
static const uint32_t TAG_ERROR_BLINK_MS = 120;

// ============================== Pins ==============================
// Wiring for ESP32-C3 Super Mini -> PN532 in HSU/UART mode:
// - 3V3       -> VCC
// - GND       -> GND
// - GPIO3 TX  -> PN532 pin labeled SCL
// - GPIO4 RX  -> PN532 pin labeled SDA
//
// IMPORTANT:
// - The PN532 board must be switched to HSU/UART mode.
// - Some PN532 boards keep the printed SCL/SDA labels even when the mode
//   switches are set to HSU/UART. We therefore follow the known-good wiring
//   from your other project exactly.
// - Reset is optional but recommended when available.
static const int PN532_RX_PIN = 4;
static const int PN532_TX_PIN = 3;
static const int PN532_RESET_PIN = 10;
static const uint32_t PN532_BAUD = 115200;

// ============================== Timing ==============================
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
static const uint32_t WIFI_PORTAL_FALLBACK_MS = 30000;
static const uint32_t WIFI_RECOVERY_RETRY_MS = 15000;
static const uint32_t WIFI_RECOVERY_ATTEMPT_MS = 6000;
static const uint32_t WIFI_STARTUP_STAGGER_MAX_MS = 2500;
static const uint32_t TAG_POLL_MS = 35;
static const uint32_t TAG_ACTIVE_WINDOW_MS = 1200;
static const uint32_t PRINTER_MOTION_QUERY_MS = 2500;
static const uint32_t PRINTER_INFO_SYNC_MS = 90000;
static const uint32_t PRINTER_FEEDER_INFO_REFRESH_MS = 2500;
static const uint32_t PRINTER_TAG_INFO_MAX_AGE_MS = 1000;
static const uint32_t PRINTER_QUERY_JITTER_MS = 450;
static const uint8_t PRINTER_QUERY_STAGGER_SLOTS = 4;
static const uint16_t PRINTER_QUERY_TIMEOUT_MS = 700;
static const uint32_t WEBHOOK_VERIFY_DELAY_MS = 1200;
static const uint32_t WEBHOOK_VERIFY_RETRY_MS = 1800;
static const uint8_t WEBHOOK_VERIFY_MAX_RETRIES = 2;
static const uint32_t WEBHOOK_RESEND_GRACE_MS = 6000;
static const uint16_t PN532_CMD_TIMEOUT_MS = 100;
static const uint16_t PN532_ACK_TIMEOUT_MS = 10;
static const uint16_t PN532_TAG_DETECT_TIMEOUT_MS = 50;
static const uint16_t PN532_MIFARE_TIMEOUT_MS = 80;
static const uint16_t PN532_FAST_READ_TIMEOUT_MS = 120;
static const uint8_t TAG_READ_SETTLE_MS = 10;
static const uint8_t NTAG_FIRST_USER_PAGE = 4;
static const uint8_t NTAG_LAST_USER_PAGE = 225;
static const size_t NTAG_USER_BYTES = (NTAG_LAST_USER_PAGE - NTAG_FIRST_USER_PAGE + 1) * 4;
static const uint8_t NTAG_CC_PAGE = 3;
static const uint8_t NTAG_INITIAL_FAST_READ_PAGES = 16;
static const uint8_t NTAG_FAST_READ_PAGES = 48;

// ============================== Network defaults ==============================
static const char* AP_SSID_BASE = "U1-Argus-Setup";
static const char* AP_PASS = ""; // open AP as requested
static const uint16_t WEB_PORT = 80;
static const byte DNS_PORT = 53;
static const uint16_t DEFAULT_PRINTER_PORT = 7125;
static const uint8_t REMOTE_READER_COUNT = 3;
static const uint8_t PREF_CONFIG_VERSION = 1;
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
  char hostname[33];
  char printerIp[64];
  uint16_t printerPort;
  uint8_t channel;
  char remoteReaders[REMOTE_READER_COUNT][96];
  uint8_t remoteReaderTools[REMOTE_READER_COUNT];
};

Settings gSettings = {
  "", "", "u1-argus-rfid", "192.168.1.10", DEFAULT_PRINTER_PORT, 0, {"", "", ""}, {0, 0, 0}
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

// ============================== PN532 ==============================
HardwareSerial PN532Serial(1);
Adafruit_PN532 nfc(PN532_RESET_PIN, &PN532Serial);
bool nfcReady = false;

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

uint32_t lastPollMs = 0;
uint32_t lastTagSeenMs = 0;
uint32_t tagLedUntilMs = 0;
uint32_t nextMotionQueryMs = 0;
uint32_t nextInfoSyncMs = 0;
uint32_t lastPrinterInfoQueryMs = 0;
uint32_t lastFeederInfoQueryMs = 0;
uint32_t pendingVerifyDueMs = 0;
uint32_t wifiLostSinceMs = 0;
uint32_t lastWifiReconnectMs = 0;
uint32_t lastWifiRecoveryMs = 0;
uint32_t stateRevision = 1;
String lastSentFingerprint = "";
String lastObservedFingerprint = "";
String pendingVerifyPayload = "";
String pendingVerifyFingerprint = "";
String pendingVerifyExpectedWithUid = "";
String pendingVerifyExpectedNoUid = "";
uint8_t pendingVerifyRetriesLeft = 0;
TagState gTagState;
PrinterChannelState gPrinterState;
WebhookState gWebhookState;

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

#if VERBOSE_DEBUG
static void debugPrint(const String& msg) {
  Serial.println(msg);
}

static void debugPrintf(const char* fmt, ...) {
  char buf[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
}
#else
#define debugPrint(msg) do {} while (0)
#define debugPrintf(...) do {} while (0)
#endif

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

static bool pn532InListPassiveTarget(uint8_t* uidOut, uint8_t& uidLenOut, uint16_t timeoutMs) {
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
  Serial.printf("[QIDI] cfg save %s: materials=%u vendors=%u bytes=%u written=%u\n",
                ok ? "ok" : "failed",
                (unsigned)gQidiMaterialCount,
                (unsigned)gQidiVendorCount,
                (unsigned)blob.length(),
                (unsigned)written);
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
  Serial.printf("[QIDI] cfg loaded: %s, materials=%u, vendors=%u, bytes=%u\n",
                gQidiCustomConfig ? "custom" : "built-in",
                (unsigned)gQidiMaterialCount,
                (unsigned)gQidiVendorCount,
                (unsigned)gQidiCfgBytes);
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
  String host = prefs.getString("hostname", "u1-argus-rfid");
  String ip = normalizedHostAddress(prefs.getString("printer_ip", "192.168.1.10"));
  uint16_t port = prefs.getUShort("printer_port", DEFAULT_PRINTER_PORT);
  uint8_t channel = prefs.getUChar("channel", 0);
  if (port == 0) port = DEFAULT_PRINTER_PORT;
  if (!isValidPrinterAddress(ip)) ip = "192.168.1.10";

  safeCopy(gSettings.wifiSsid, sizeof(gSettings.wifiSsid), ssid);
  safeCopy(gSettings.wifiPass, sizeof(gSettings.wifiPass), pass);
  safeCopy(gSettings.hostname, sizeof(gSettings.hostname), host);
  safeCopy(gSettings.printerIp, sizeof(gSettings.printerIp), ip);
  gSettings.printerPort = port;
  gSettings.channel = (channel > 3) ? 0 : channel;
  for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
    String key = String("reader_") + String(i + 2);
    String val = prefs.getString(key.c_str(), "");
    safeCopy(gSettings.remoteReaders[i], sizeof(gSettings.remoteReaders[i]), val);

    String toolKey = String("reader_tool_") + String(i + 2);
    uint8_t tool = prefs.getUChar(toolKey.c_str(), 0);
    gSettings.remoteReaderTools[i] = (tool >= 1 && tool <= 4) ? tool : 0;
  }
  prefs.end();
}

static void saveSettings() {
  prefs.begin(PREF_NS, false);
  prefs.putUChar("cfg_ver", PREF_CONFIG_VERSION);
  prefs.putString("wifi_ssid", gSettings.wifiSsid);
  prefs.putString("wifi_pass", gSettings.wifiPass);
  prefs.putString("hostname", gSettings.hostname);
  prefs.putString("printer_ip", gSettings.printerIp);
  prefs.putUShort("printer_port", gSettings.printerPort);
  prefs.putUChar("channel", gSettings.channel);
  for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
    String key = String("reader_") + String(i + 2);
    prefs.putString(key.c_str(), gSettings.remoteReaders[i]);

    String toolKey = String("reader_tool_") + String(i + 2);
    prefs.putUChar(toolKey.c_str(), gSettings.remoteReaderTools[i]);
  }
  prefs.end();
}

static String filamentDetectUrl() {
  return printerBaseUrl() + "/printer/filament_detect/set";
}

static uint8_t printerChannelIndex() {
  return gSettings.channel < 4 ? gSettings.channel : 0;
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

static bool httpGetPrinterUrl(const String& url, String& resp, int& httpCode) {
  if (WiFi.status() != WL_CONNECTED) {
    setPrinterStateDisconnected("Wi-Fi disconnected");
    return false;
  }

  debugPrintf("[DEBUG] PRINTER QUERY %s\n", url.c_str());

  HTTPClient http;
  http.setTimeout(PRINTER_QUERY_TIMEOUT_MS);
  if (!http.begin(url)) {
    setPrinterStateDisconnected("HTTP begin failed");
    return false;
  }

  httpCode = http.GET();
  resp = (httpCode > 0) ? http.getString() : "";
  http.end();

  debugPrintf("[DEBUG] PRINTER QUERY RESULT ok=%d code=%d body=%s\n",
              httpCode > 0 ? 1 : 0,
              httpCode,
              resp.c_str());

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
    lastPrinterInfoQueryMs = millis();
  }
  return gPrinterState.queryOk && gPrinterState.hasInfo;
}

static bool fetchPrinterMotionState() {
  String url = printerMotionQueryUrl();
  String resp;
  int httpCode = 0;
  if (!httpGetPrinterUrl(url, resp, httpCode)) {
    markPrinterQueryError(url, httpCode, "HTTP motion GET failed", false);
    return false;
  }
  return updatePrinterMotionStateFromJson(resp, httpCode, url);
}

static bool fetchPrinterInfoState(const char* reason) {
  String url = printerFilamentInfoQueryUrl();
  debugPrintf("[DEBUG] FILAMENT INFO QUERY reason=%s\n", reason ? reason : "unknown");
  String resp;
  int httpCode = 0;
  if (!httpGetPrinterUrl(url, resp, httpCode)) {
    markPrinterQueryError(url, httpCode, "HTTP filament GET failed", true);
    return false;
  }
  return updatePrinterInfoStateFromJson(resp, httpCode, url);
}

static bool fetchPrinterChannelState() {
  return fetchPrinterInfoState("compat");
}

static void scheduleNextMotionQuery(uint32_t now) {
  nextMotionQueryMs = now + PRINTER_MOTION_QUERY_MS + printerQueryJitterMs();
}

static void scheduleNextInfoSync(uint32_t now) {
  nextInfoSyncMs = now + PRINTER_INFO_SYNC_MS + printerQueryJitterMs();
}

static void scheduleInitialPrinterQueries(uint32_t now) {
  uint32_t phase = printerQueryPhaseOffsetMs();
  nextMotionQueryMs = now + phase + printerQueryJitterMs();
  nextInfoSyncMs = now + phase + 500 + printerQueryJitterMs();
}

static bool ensureFreshPrinterInfo(const char* reason, uint32_t maxAgeMs) {
  if (WiFi.status() != WL_CONNECTED || portalMode) return false;
  if (lastPrinterInfoQueryMs != 0 && (millis() - lastPrinterInfoQueryMs) <= maxAgeMs) {
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
  pendingVerifyPayload = "";
  pendingVerifyFingerprint = "";
  pendingVerifyExpectedWithUid = "";
  pendingVerifyExpectedNoUid = "";
  pendingVerifyDueMs = 0;
  pendingVerifyRetriesLeft = 0;
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

  JsonObject printer = doc["printer"].to<JsonObject>();
  printer["channel"] = gSettings.channel;
  printer["tool_head"] = gSettings.channel + 1;
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
  body += F("<title>U1 Argus RFID Setup</title><style>:root{--bg:#f4efe5;--panel:#fffaf1;--ink:#18231e;--muted:#5d6a62;--line:#d8cdb7;--accent:#b85c38;--accent2:#2f5d50}*{box-sizing:border-box}body{font-family:'Trebuchet MS',Verdana,sans-serif;max-width:820px;margin:0 auto;padding:18px 14px 48px;background:radial-gradient(circle at top,#fff9ee 0,#f4efe5 48%,#ece3d3 100%);color:var(--ink)}.shell{background:var(--panel);border:1px solid var(--line);border-radius:22px;padding:22px;box-shadow:0 18px 44px rgba(67,49,26,.10)}label{display:block;margin-top:12px;font-weight:700}input,select{width:100%;padding:12px 13px;margin-top:5px;border:1px solid #cdbfa6;border-radius:12px;background:#fffdf8}button,.btn{display:inline-block;margin-top:16px;padding:11px 15px;border:none;border-radius:999px;background:var(--accent);color:#fff;text-decoration:none;font-weight:700}.btn.alt{background:var(--accent2)}small,.muted{color:var(--muted)}code{background:#efe4d0;padding:2px 6px;border-radius:6px}.help{display:block;margin-top:5px;line-height:1.35}.top{display:flex;justify-content:space-between;gap:12px;align-items:center;flex-wrap:wrap}.topRight{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.msg{padding:12px 14px;border-radius:12px;background:#f7e9d4;border:1px solid #e7d0ac;margin:14px 0}.group{margin-top:18px;padding-top:8px;border-top:1px dashed #d8cdb7}</style></head><body>");
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
  body += String(F("<label>")) + "Hostname (mDNS, no .local)" + F("</label><input name='hostname' maxlength='32' required value='"); body += htmlEscape(gSettings.hostname); body += F("'>");
  body += String(F("<label>")) + "Snapmaker U1 address" + F("</label><input name='printer_ip' maxlength='63' required placeholder='192.168.1.120 or u1.local' value='"); body += htmlEscape(gSettings.printerIp); body += F("'><small class='help'>");
  body += "IP or mDNS hostname, e.g. 192.168.1.120 or u1.local";
  body += F("</small>");
  body += String(F("<label>")) + "Snapmaker U1 Port" + F("</label><input name='printer_port' type='number' min='1' max='65535' required value='"); body += String(gSettings.printerPort); body += F("'>");
  body += String(F("<label>")) + "Tool Head" + F("</label><select name='channel' id='channel'>");
  for (int i = 0; i < 4; i++) {
    body += "<option value='" + String(i) + "'" + String(gSettings.channel == i ? " selected" : "") + ">";
    body += "Tool Head ";
    body += String(i + 1);
    body += F("</option>");
  }
  body += F("</select>");
  body += String(F("<div class='group'><p><b>")) + "Additional U1 Argus Remote Readers" + F("</b><br><small>") + "Optional: IP or full URL plus Tool Head for up to three more dashboard buttons." + F("</small></p>");
  body += String(F("<button type='button' class='btn alt' id='prefillReaders'>")) + "Prefill RFID readers from mDNS name" + F("</button><br><small class='help'>");
  body += "Fills only empty reader fields and skips this reader's own Tool Head.";
  body += F("</small>");
  for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
    body += String(F("<label>")) + "Reader ";
    body += String(i + 2);
    body += " IP or URL";
    body += F("</label><input name='reader_");
    body += String(i + 2);
    body += F("' maxlength='95' placeholder='");
    body += "e.g. 192.168.1.51 or http://u1-argus-2.local/";
    body += F("' value='");
    body += htmlEscape(gSettings.remoteReaders[i]);
    body += F("'>");

    body += String(F("<label>")) + "Reader ";
    body += String(i + 2);
    body += F(" ");
    body += "Tool Head";
    body += F("</label><select name='reader_tool_");
    body += String(i + 2);
    body += F("'><option value='0'");
    if (gSettings.remoteReaderTools[i] == 0) body += F(" selected");
    body += F(">");
    body += "Not assigned";
    body += F("</option>");
    for (uint8_t tool = 1; tool <= 4; tool++) {
      body += F("<option value='");
      body += String(tool);
      body += F("'");
      if (gSettings.remoteReaderTools[i] == tool) body += F(" selected");
      body += F(">");
      body += "Tool Head ";
      body += String(tool);
      body += F("</option>");
    }
    body += F("</select>");
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
  body += F("<script>function u1aCleanHost(v){v=(v||'').trim();v=v.replace(/^https?:\\/\\//i,'');v=v.split('/')[0];v=v.split(':')[0];v=v.replace(/\\.local$/i,'');return v;}function u1aPrefillReaders(){const host=u1aCleanHost(document.querySelector('[name=hostname]').value);const ch=document.querySelector('[name=channel]');const current=Number(ch?ch.value:0)+1;if(!host||current<1||current>4)return;const m=host.match(/^(.*?)([0-9]+)$/);let base=host;if(m&&Number(m[2])===current)base=m[1];if(!base)base=host;let slot=2;for(let tool=1;tool<=4;tool++){if(tool===current)continue;const input=document.querySelector('[name=reader_'+slot+']');const sel=document.querySelector('[name=reader_tool_'+slot+']');if(input&&!input.value.trim()){input.value='http://'+base+tool+'.local';if(sel&&(!sel.value||sel.value==='0'))sel.value=String(tool);}slot++;}}const prefillBtn=document.getElementById('prefillReaders');if(prefillBtn)prefillBtn.addEventListener('click',u1aPrefillReaders);</script>");
  body += F("</div>");
  body += F("</body></html>");
  return body;
}

static String dashboardPageHtml() {
  String body;
  body.reserve(8000);
  String dashboardUrl = String("http://") + configuredMdnsName();
  String currentToolHead = String("Tool Head ") + String(gSettings.channel + 1);
  body += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  body += F("<title>U1 Argus RFID Status</title><style>:root{--bg:#f6f1e6;--panel:#fffaf2;--ink:#1a241f;--muted:#617068;--line:#d9cdb8;--accent:#b65f3b;--accent2:#2f5d50;--ok:#3f7d4f;--warn:#c2862a;--bad:#b64242}*{box-sizing:border-box}body{margin:0;font-family:'Trebuchet MS',Verdana,sans-serif;background:radial-gradient(circle at top,#fffaf0 0,#f1eadc 44%,#e8decf 100%);color:var(--ink)}main{max-width:1080px;margin:0 auto;padding:18px 14px 40px}.hero{display:grid;grid-template-columns:1.3fr .7fr;gap:14px}.panel{background:var(--panel);border:1px solid var(--line);border-radius:24px;padding:18px;box-shadow:0 18px 44px rgba(67,49,26,.10)}h1,h2,h3,p{margin:0}h1{font-size:1.9rem;line-height:1.05}.sub{margin-top:10px;color:var(--muted)}.meta{display:flex;gap:10px;flex-wrap:wrap;margin-top:16px}.chip{display:inline-flex;align-items:center;gap:8px;padding:8px 12px;border-radius:999px;background:#efe4d0;color:#2b312d;font-weight:700;font-size:.92rem}.dot{width:10px;height:10px;border-radius:50%;background:#bbb}.ok{background:var(--ok)}.warn{background:var(--warn)}.bad{background:var(--bad)}.actions{display:flex;justify-content:flex-start;align-items:flex-start}.readerNav{display:flex;flex-wrap:wrap;gap:10px}.btn{display:inline-block;padding:11px 15px;border-radius:999px;background:var(--accent2);color:#fff;text-decoration:none;font-weight:700}.btn.secondary{background:var(--accent)}.btn.ghost{background:#e9dcc7;color:#30463d}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px;margin-top:14px}.cardTitle{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:14px}.stamp{font-size:.88rem;color:var(--muted)}.kv{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px 14px}.item{padding:10px 12px;border-radius:16px;background:#f6efe2;border:1px solid #e7dbc7}.label{font-size:.78rem;color:var(--muted);text-transform:uppercase;letter-spacing:.05em}.value{margin-top:4px;font-size:1rem;font-weight:700;word-break:break-word}.accent{color:var(--accent2)}.swatch{display:inline-flex;align-items:center;gap:8px}.sw{width:16px;height:16px;border-radius:50%;border:1px solid rgba(0,0,0,.15);background:#ddd}.json{margin-top:14px;padding:14px;border-radius:18px;background:#171c19;color:#d7efe5;white-space:pre-wrap;word-break:break-word;font-family:Consolas,'Courier New',monospace;font-size:.84rem;max-height:240px;overflow:auto}.foot{margin-top:14px;color:var(--muted);font-size:.9rem}@media (max-width:860px){.hero,.grid,.kv{grid-template-columns:1fr}.actions{justify-content:flex-start}}</style></head><body><main>");
  body += String(F("<section class='hero'><div class='panel'><h1>U1 Argus Remote RFID</h1><p class='sub'>")) + "Live status for the printer channel and the last OpenSpool or QIDI tag that was read. The page only refreshes when the state actually changes." + F("</p><div class='meta'><span class='chip'><span class='dot warn' id='wifiDot'></span><span id='wifiText'>") + "Checking Wi-Fi" + F("</span></span><span class='chip'><span class='dot ok'></span><span>URL: ");
  body += htmlEscape(dashboardUrl);
  body += String(F("</span></span><span class='chip'><span class='dot warn' id='printerDot'></span><span id='printerText'>")) + "Loading printer status" + F("</span></span><span class='chip'><span class='dot warn' id='tagDot'></span><span id='tagText'>") + "Loading tag status" + F("</span></span></div></div><div class='panel actions'><div class='readerNav'>");
  body += F("<h1 style='width:100%'>");
  body += htmlEscape(currentToolHead);
  body += F("</h1>");
  body += String(F("<a class='btn' href='/setup'>")) + "Setup" + F("</a>");
  for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
    String remoteUrl = normalizedReaderUrl(gSettings.remoteReaders[i]);
    if (!remoteUrl.length()) continue;
    body += F("<a class='btn ghost' href='");
    body += htmlEscape(remoteUrl);
    body += F("'>");
    uint8_t remoteTool = gSettings.remoteReaderTools[i];
    if (remoteTool >= 1 && remoteTool <= 4) {
      body += "Tool Head ";
      body += String(remoteTool);
    } else {
      body += "Reader ";
      body += String(i + 2);
    }
    body += F("</a>");
  }
  body += F("</div></div></section>");
  body += String(F("<section class='grid'><article class='panel'><div class='cardTitle'><div><h2>")) + "Printer Tool Head" + F("</h2><p class='stamp' id='printerStamp'>") + "No response yet" + F("</p></div><span class='chip'><span id='channelId'>-</span></span></div><div class='kv' id='printerKv'></div><div class='json' id='printerJson'>") + "No printer data yet." + F("</div></article><article class='panel'><div class='cardTitle'><div><h2>") + "Tag Reader" + F("</h2><p class='stamp' id='tagStamp'>") + "No valid tag read yet" + F("</p></div><span class='chip' id='tagBadge'>") + "no tag" + F("</span></div><div class='kv' id='tagKv'></div><div class='json' id='tagJson'>") + "No tag data yet." + F("</div></article></section>");
  body += String(F("<section class='grid'><article class='panel'><div class='cardTitle'><div><h2>Webhook</h2><p class='stamp' id='hookStamp'>")) + "Nothing sent yet" + F("</p></div><span class='chip' id='hookBadge'>idle</span></div><div class='kv' id='hookKv'></div><div class='json' id='hookJson'>") + "No payload sent yet." + F("</div></article><article class='panel'><div class='cardTitle'><div><h2>") + "Network" + F("</h2><p class='stamp'>ESP32-C3 ") + "status" + F("</p></div><span class='chip accent' id='revBadge'>rev -</span></div><div class='kv' id='netKv'></div><p class='foot'>Firmware ");
  body += FW_VERSION;
  body += F("</p></article></section>");
  body += F("<script>");
  body += F("const T={wifiChecking:'Checking Wi-Fi',wifiConnected:'Wi-Fi ',wifiDisconnected:'Wi-Fi disconnected',printerLoading:'Loading printer status',printerResponding:'Printer responding',printerNoStatus:'No printer status',tagLoading:'Loading tag status',tagActive:'Tag actively detected',tagStored:'last tag stored',tagNone:'no tag',noResponse:'No response yet',updatedAgo:'updated ',statusError:'Status error: ',unknown:'unknown',source:'Source',vendor:'Vendor',manufacturer:'Manufacturer',material:'Material',subType:'Sub Type',color:'Color',nozzle:'Nozzle',bed:'Bed',filamentSensor:'Filament Sensor',official:'Official',filamentYes:'Filament detected',filamentNo:'No filament',yes:'yes',no:'no',noPrinterData:'No printer data yet.',lastValidTag:'last valid tag ',noValidTag:'No valid tag read yet',ready:'ready',stored:'stored',noTagData:'No tag data yet.',payloadStatus:'Payload Status',lastSendTry:'last send attempt ',nothingSent:'Nothing sent yet',successful:'successful',failed:'failed',result:'Result',target:'Target',channel:'Channel',httpCode:'HTTP Code',idle:'idle',errorShort:'error',noPayload:'No payload sent yet.',ssid:'SSID',ip:'IP',hostname:'Hostname',rssi:'RSSI',mode:'Mode',printerPort:'Printer Port',webUpdateError:'Web update error',seconds:' s',minutes:' min',hours:' h'};");
  body += F("let etag='';const q=s=>document.querySelector(s);const esc=s=>String(s??'').replace(/[&<>\"]/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[m]));const age=ms=>{if(ms==null)return'-';const s=Math.max(0,Math.round(ms/1000));if(s<60)return s+T.seconds;const m=Math.floor(s/60);if(m<60)return m+T.minutes;const h=Math.floor(m/60);return h+T.hours;};const kv=(rows)=>rows.map(r=>`<div class='item'><div class='label'>${esc(r[0])}</div><div class='value'>${r[1]}</div></div>`).join('');const sw=(hex)=>hex?`<span class='swatch'><span class='sw' style='background:${esc(hex)}'></span>${esc(hex)}</span>`:'-';const badge=(dotSel,textSel,ok,warn,text)=>{const dot=q(dotSel);dot.className='dot '+(ok?'ok':warn?'warn':'bad');q(textSel).textContent=text;};const toolHead=d=>{const ch=Number(d?.printer?.channel??0);const head=Number(d?.printer?.tool_head??(ch+1));return ('Tool Head ')+head+' (Channel '+ch+')';};function render(d){q('#revBadge').textContent='rev '+d.revision;badge('#wifiDot','#wifiText',d.wifi.connected,!d.wifi.connected,d.wifi.connected?(T.wifiConnected+(d.wifi.ip||'')):T.wifiDisconnected);badge('#printerDot','#printerText',d.printer.query_ok,!d.printer.query_ok,d.printer.query_ok?T.printerResponding:(d.printer.error||T.printerNoStatus));badge('#tagDot','#tagText',d.tag.active,d.tag.has_data,d.tag.active?T.tagActive:(d.tag.has_data?T.tagStored:T.tagNone));q('#channelId').textContent=toolHead(d);q('#printerStamp').textContent=d.printer.query_ok?(T.updatedAgo+age(d.printer.age_ms)):(T.statusError+(d.printer.error||T.unknown));q('#printerKv').innerHTML=kv([[T.vendor,esc(d.printer.vendor||'-')],[T.manufacturer,esc(d.printer.manufacturer||'-')],[T.material,esc(d.printer.main_type||'-')],[T.subType,esc(d.printer.sub_type||'-')],[T.color,sw(d.printer.color_hex)],[T.nozzle,esc(d.printer.min_temp!=null&&d.printer.max_temp!=null?`${d.printer.min_temp} - ${d.printer.max_temp} C`:'-')],[T.bed,esc(d.printer.bed_temp!=null?`${d.printer.bed_temp} C`:'-')],[T.filamentSensor,esc(d.printer.filament_detected==null?'-':(d.printer.filament_detected?T.filamentYes:T.filamentNo))],[T.official,esc(d.printer.official==null?'-':(d.printer.official?T.yes:T.no))]]);q('#printerJson').textContent=d.printer.raw_json||T.noPrinterData;q('#tagStamp').textContent=d.tag.has_data?(T.lastValidTag+age(d.tag.age_ms)):T.noValidTag;q('#tagBadge').textContent=d.tag.active?T.ready:T.tagStored;q('#tagKv').innerHTML=kv([['UID',esc(d.tag.uid||'-')],[T.source,esc(d.tag.source||'-')],[T.vendor,esc(d.tag.vendor||'-')],[T.material,esc(d.tag.main_type||'-')],[T.subType,esc(d.tag.sub_type||'-')],[T.color,sw(d.tag.color_hex)],[T.nozzle,esc(d.tag.min_temp!=null&&d.tag.max_temp!=null?`${d.tag.min_temp} - ${d.tag.max_temp} C`:'-')],[T.bed,esc(d.tag.bed_temp!=null?`${d.tag.bed_temp} C`:'-')],[T.payloadStatus,esc(d.tag.active?T.ready:T.stored)]]);q('#tagJson').textContent=d.tag.openspool_json||T.noTagData;q('#hookStamp').textContent=d.webhook.known?(T.lastSendTry+age(d.webhook.age_ms)):T.nothingSent;q('#hookBadge').textContent=d.webhook.known?(d.webhook.ok?'ok':T.errorShort):T.idle;q('#hookKv').innerHTML=kv([[T.httpCode,esc(d.webhook.http_code??'-')],[T.result,esc(d.webhook.known?(d.webhook.ok?T.successful:T.failed):'-')],[T.target,esc(d.printer.endpoint||'-')],[T.channel,esc(toolHead(d))]]);q('#hookJson').textContent=d.tag.mapped_payload||d.webhook.response||T.noPayload;q('#netKv').innerHTML=kv([[T.ssid,esc(d.wifi.ssid||'-')],[T.ip,esc(d.wifi.ip||'-')],[T.hostname,esc(d.wifi.hostname||'-')],[T.rssi,esc(d.wifi.rssi==null?'-':`${d.wifi.rssi} dBm`)],[T.mode,esc(d.mode||'-')],[T.printerPort,esc(String(d.printer.port||'-'))]]);}async function refresh(){try{const h={};if(etag)h['If-None-Match']=etag;const r=await fetch('/api/state',{cache:'no-store',headers:h});if(r.status===304)return;if(!r.ok)throw new Error('HTTP '+r.status);etag=r.headers.get('ETag')||etag;render(await r.json());}catch(e){q('#printerText').textContent=T.webUpdateError;q('#printerDot').className='dot bad';}}refresh();setInterval(refresh,2000);setInterval(()=>location.reload(),30000);</script>");
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
    String hostname = web.arg("hostname"); hostname.trim();
    String printerIp = web.arg("printer_ip"); printerIp.trim();
    printerIp = normalizedHostAddress(printerIp);
    String portStr = web.arg("printer_port"); portStr.trim();
    String channelStr = web.arg("channel"); channelStr.trim();
    String readerUrls[REMOTE_READER_COUNT];
    uint8_t readerTools[REMOTE_READER_COUNT];
    for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
      readerUrls[i] = web.arg(String("reader_") + String(i + 2));
      readerUrls[i].trim();

      String toolStr = web.arg(String("reader_tool_") + String(i + 2));
      toolStr.trim();
      int tool = toolStr.toInt();
      readerTools[i] = (tool >= 1 && tool <= 4) ? (uint8_t)tool : 0;
    }

    uint32_t portVal = portStr.toInt();
    int ch = channelStr.toInt();

    if (ssid.length() == 0) {
      sendHtmlNoCache(400, configPageHtml("SSID must not be empty."));
      return;
    }
    if (hostname.length() == 0) {
      sendHtmlNoCache(400, configPageHtml("Hostname must not be empty."));
      return;
    }
    if (ch < 0 || ch > 3) {
      sendHtmlNoCache(400, configPageHtml("Tool Head selection is invalid."));
      return;
    }
    if (portVal == 0 || portVal > 65535 || !parseIpPort(printerIp.c_str(), (uint16_t)portVal)) {
      sendHtmlNoCache(400, configPageHtml("Printer address/port is invalid."));
      return;
    }

    safeCopy(gSettings.wifiSsid, sizeof(gSettings.wifiSsid), ssid);
    safeCopy(gSettings.wifiPass, sizeof(gSettings.wifiPass), pass);
    safeCopy(gSettings.hostname, sizeof(gSettings.hostname), hostname);
    safeCopy(gSettings.printerIp, sizeof(gSettings.printerIp), printerIp);
    cachedPrinterAddress = "";
    cachedPrinterHost = "";
    cachedPrinterResolveOk = false;
    gSettings.printerPort = (uint16_t)portVal;
    gSettings.channel = (uint8_t)ch;
    for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
      safeCopy(gSettings.remoteReaders[i], sizeof(gSettings.remoteReaders[i]), readerUrls[i]);
      gSettings.remoteReaderTools[i] = readerTools[i];
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

static void beginStationServices(const char* reason) {
  if (portalMode) {
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    portalMode = false;
    Serial.printf("\n[PORTAL] Setup hotspot stopped after Wi-Fi recovery (%s)\n", reason ? reason : "connected");
  }

  Serial.printf("\n[WIFI] Connected: %s\n", WiFi.localIP().toString().c_str());
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
    WiFi.setAutoReconnect(true);
    WiFi.begin(gSettings.wifiSsid, gSettings.wifiPass);
    lastWifiRecoveryMs = millis();
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
  WiFi.setAutoReconnect(true);
  WiFi.begin(gSettings.wifiSsid, gSettings.wifiPass);

  if (!waitForWifiConnect(WIFI_CONNECT_TIMEOUT_MS, false)) {
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
  WiFi.setAutoReconnect(true);
  WiFi.begin(gSettings.wifiSsid, gSettings.wifiPass);

  if (waitForWifiConnect(WIFI_RECOVERY_ATTEMPT_MS, true)) {
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
    Serial.printf("[WIFI] Lost connection, status=%d\n", (int)WiFi.status());
  }

  if (lastWifiReconnectMs == 0 || (now - lastWifiReconnectMs) >= WIFI_RECOVERY_RETRY_MS) {
    lastWifiReconnectMs = now;
    Serial.printf("[WIFI] Reconnect retry: SSID=%s\n", gSettings.wifiSsid);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(gSettings.wifiSsid, gSettings.wifiPass);
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

static bool readOpenSpoolUserArea(uint8_t* out, size_t outLen, size_t& actualLen) {
  if (readOpenSpoolUserAreaFast(out, outLen, actualLen)) return true;
  debugPrint("[DEBUG] NTAG FAST_READ unavailable, falling back to 4-page windows");
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

static bool readQidiFromCurrentTag(const uint8_t* uid, uint8_t uidLen, String& outJson) {
  if (uidLen != 4) return false;
  uint32_t readStartMs = millis();

  if (!pn532MifareClassicAuthBlock(uid, uidLen, QIDI_DATA_BLOCK)) {
    debugPrint("[DEBUG] QIDI auth failed");
    return false;
  }

  uint8_t data[16] = {0};
  if (!pn532MifareClassicReadBlock(QIDI_DATA_BLOCK, data)) {
    debugPrint("[DEBUG] QIDI block read failed");
    return false;
  }

  bool ok = buildQidiTagJson(data[0], data[1], data[2], outJson);
  if (ok) {
    Serial.printf("[NFC] QIDI read uid=%s material=%u color=%u vendor=%u time=%lu ms\n",
                  bytesToHexString(uid, uidLen).c_str(),
                  (unsigned)data[0],
                  (unsigned)data[1],
                  (unsigned)data[2],
                  (unsigned long)(millis() - readStartMs));
    debugPrintf("[DEBUG] QIDI tag mapped material=%u color=%u vendor=%u json=%s\n",
                data[0], data[1], data[2], outJson.c_str());
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
  outFingerprint += String(gSettings.channel);
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
  fp += String(gSettings.channel);
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
  req["channel"] = gSettings.channel;
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
    lastSentFingerprint = fingerprint;
  } else if (pulseOnFail) {
    pulseTagLedError();
  }
  return hookOk;
}

static void scheduleSetVerification(const String& payload,
                                    const String& fingerprint,
                                    const String& expectedWithUid,
                                    const String& expectedNoUid) {
  pendingVerifyPayload = payload;
  pendingVerifyFingerprint = fingerprint;
  pendingVerifyExpectedWithUid = expectedWithUid;
  pendingVerifyExpectedNoUid = expectedNoUid;
  pendingVerifyRetriesLeft = WEBHOOK_VERIFY_MAX_RETRIES;
  pendingVerifyDueMs = millis() + WEBHOOK_VERIFY_DELAY_MS;
  debugPrintf("[DEBUG] SET verification scheduled in %lu ms\n", (unsigned long)WEBHOOK_VERIFY_DELAY_MS);
}

static void processPendingSetVerification() {
  if (pendingVerifyDueMs == 0 || portalMode || WiFi.status() != WL_CONNECTED) return;
  uint32_t now = millis();
  if ((int32_t)(now - pendingVerifyDueMs) < 0) return;

  if (fetchPrinterInfoState("set verify") && currentPrinterMatchesExpected(pendingVerifyExpectedWithUid, pendingVerifyExpectedNoUid)) {
    debugPrint("[DEBUG] SET verified on printer");
    clearPendingVerification();
    scheduleNextInfoSync(millis());
    return;
  }

  if (pendingVerifyRetriesLeft == 0) {
    debugPrint("[DEBUG] SET verification failed, retries exhausted");
    pulseTagLedError();
    clearPendingVerification();
    return;
  }

  pendingVerifyRetriesLeft--;
  debugPrintf("[DEBUG] SET verification mismatch, retrying payload, retries_left=%u\n", pendingVerifyRetriesLeft);
  if (sendFilamentSetPayload(pendingVerifyPayload, pendingVerifyFingerprint, "verify retry", false)) {
    pendingVerifyDueMs = millis() + WEBHOOK_VERIFY_RETRY_MS;
  } else if (pendingVerifyRetriesLeft == 0) {
    pulseTagLedError();
    clearPendingVerification();
  } else {
    pendingVerifyDueMs = millis() + WEBHOOK_VERIFY_RETRY_MS;
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

  lastTagSeenMs = millis();
  storeTagState(fields, openspoolJson, uid, uidLen, payload, fingerprint, source);

  if (fingerprint != lastObservedFingerprint) {
    lastObservedFingerprint = fingerprint;
    debugPrintf("[DEBUG] NFC UID %s\n", bytesToHexString(uid, uidLen).c_str());
    debugPrintf("[DEBUG] NFC %s JSON %s\n", source ? source : "tag", openspoolJson.c_str());
    debugPrintf("[DEBUG] NFC mapped payload %s\n", payload.c_str());
  }

  bool printerInfoKnown = ensureFreshPrinterInfo("tag compare", PRINTER_TAG_INFO_MAX_AGE_MS);
  String expectedWithUid;
  String expectedNoUid;
  bool comparableOk = buildDesiredPrinterComparableFingerprint(fields, uid, uidLen, expectedWithUid, true) &&
                      buildDesiredPrinterComparableFingerprint(fields, uid, uidLen, expectedNoUid, false);

  bool sentBefore = fingerprint == lastSentFingerprint;
  bool printerMatches = comparableOk && currentPrinterMatchesExpected(expectedWithUid, expectedNoUid);
  if (printerMatches) {
    lastSentFingerprint = fingerprint;
    clearPendingVerification();
    debugPrint("[DEBUG] Printer already matches tag, webhook skipped");
    return;
  }

  if (comparableOk && gPrinterState.queryOk && gPrinterState.hasInfo) {
    debugPrintf("[DEBUG] Printer channel differs from tag, SET allowed. desired=%s current=%s\n",
                expectedWithUid.c_str(),
                currentPrinterComparableFingerprint().c_str());
  }

  if (sentBefore && !printerInfoKnown) {
    debugPrint("[DEBUG] Payload unchanged, printer state unknown, webhook skipped");
    return;
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
static bool readOpenSpoolFromCurrentTag(uint8_t* uid, uint8_t& uidLen, String& outJson) {
  uint32_t readStartMs = millis();
  uidLen = 0;
  if (!pn532InListPassiveTarget(uid, uidLen, PN532_TAG_DETECT_TIMEOUT_MS)) {
    return false;
  }

  pulseTagLed();
  debugPrintf("[DEBUG] NFC tag detected, UID=%s\n", bytesToHexString(uid, uidLen).c_str());
  if (uidLen == 4) {
    return false;
  }
  delay(TAG_READ_SETTLE_MS);

  static uint8_t buf[NTAG_USER_BYTES];
  size_t bufLen = 0;
  if (!readOpenSpoolUserArea(buf, sizeof(buf), bufLen)) {
    debugPrint("[DEBUG] NTAG user area read failed");
    if (uidLen != 4) pulseTagLedError();
    return false;
  }

  size_t ndefOffset = 0, ndefLen = 0;
  if (!findNdefTlv(buf, bufLen, ndefOffset, ndefLen)) {
    debugPrint("[DEBUG] No NDEF TLV found");
    if (uidLen != 4) pulseTagLedError();
    return false;
  }

  String mime;
  outJson = "";
  if (!parseMimeRecord(buf + ndefOffset, ndefLen, mime, outJson)) {
    debugPrint("[DEBUG] Failed to parse NDEF MIME record");
    if (uidLen != 4) pulseTagLedError();
    return false;
  }
  debugPrintf("[DEBUG] NDEF MIME=%s length=%u\n", mime.c_str(), (unsigned)outJson.length());
  if (mime != "application/json") {
    debugPrint("[DEBUG] Ignored tag because MIME is not application/json");
    if (uidLen != 4) pulseTagLedError();
    return false;
  }

  // Avoid parsing extended OpenSpool JSON twice; the mapping step validates protocol strictly.
  debugPrintf("[DEBUG] NFC JSON read completed in %lu ms, raw=%u bytes, payload=%u bytes\n",
              (unsigned long)(millis() - readStartMs),
              (unsigned)bufLen,
              (unsigned)outJson.length());
  Serial.printf("[NFC] OpenSpool read uid=%s time=%lu ms raw=%u payload=%u bytes\n",
                bytesToHexString(uid, uidLen).c_str(),
                (unsigned long)(millis() - readStartMs),
                (unsigned)bufLen,
                (unsigned)outJson.length());
  return true;
}

static void processPrinterPolling() {
  if (portalMode || WiFi.status() != WL_CONNECTED) return;

  uint32_t now = millis();
  if (nextMotionQueryMs == 0 || nextInfoSyncMs == 0) {
    scheduleInitialPrinterQueries(now);
  }

  if ((int32_t)(now - nextMotionQueryMs) >= 0) {
    bool motionOk = fetchPrinterMotionState();
    now = millis();
    scheduleNextMotionQuery(now);

    if (motionOk && gPrinterState.motionKnown && gPrinterState.filamentDetected) {
      if (lastFeederInfoQueryMs == 0 || (now - lastFeederInfoQueryMs) >= PRINTER_FEEDER_INFO_REFRESH_MS) {
        lastFeederInfoQueryMs = now;
        if (fetchPrinterInfoState("feeder active")) {
          scheduleNextInfoSync(millis());
        }
      }
    }
  }

  now = millis();
  if ((int32_t)(now - nextInfoSyncMs) >= 0) {
    fetchPrinterInfoState("slow sync");
    scheduleNextInfoSync(millis());
  }
}

// ============================== Setup / Loop ==============================
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(400);
  randomSeed((uint32_t)ESP.getEfuseMac() ^ micros());
  Serial.printf("\n[%s] boot %s\n", FW_NAME, FW_VERSION);
  debugPrint("[DEBUG] Serial monitor ready");
  debugPrintf("[DEBUG] PN532 HSU wiring RX=GPIO%d TX=GPIO%d reset=GPIO%d baud=%lu\n",
              PN532_RX_PIN,
              PN532_TX_PIN,
              PN532_RESET_PIN,
              PN532_BAUD);
  debugPrintf("[DEBUG] Tag activity LED pin=%d active=%s\n",
              TAG_LED_PIN,
              TAG_LED_ACTIVE_HIGH ? "HIGH" : "LOW");

  pinMode(TAG_LED_PIN, OUTPUT);
  setTagLed(false);

  loadSettings();
  loadQidiConfig();
  gPrinterState.endpoint = printerChannelQueryUrl();
  Serial.printf("[CONFIG] SSID=%s, pass=%u chars, host=%s%s, printer=%s:%u (%s), channel=%u, tool_head=%u, query_offset=%lu ms, setup_ap=%s\n",
                strlen(gSettings.wifiSsid) ? gSettings.wifiSsid : "(empty)",
                (unsigned)strlen(gSettings.wifiPass),
                strlen(gSettings.hostname) ? gSettings.hostname : "(empty)",
                strlen(gSettings.hostname) ? ".local" : "",
                strlen(gSettings.printerIp) ? gSettings.printerIp : "(empty)",
                (unsigned)gSettings.printerPort,
                printerAddressType(String(gSettings.printerIp)),
                (unsigned)gSettings.channel,
                (unsigned)(gSettings.channel + 1),
                (unsigned long)printerQueryPhaseOffsetMs(),
                setupApSsid());
  Serial.printf("[QIDI] cfg=%s, materials=%u, vendors=%u\n",
                gQidiCustomConfig ? "custom" : "built-in",
                (unsigned)(gQidiCustomConfig ? gQidiMaterialCount : (sizeof(DEFAULT_QIDI_MATERIALS) / sizeof(DEFAULT_QIDI_MATERIALS[0]))),
                (unsigned)(gQidiCustomConfig ? gQidiVendorCount : (sizeof(DEFAULT_QIDI_VENDORS) / sizeof(DEFAULT_QIDI_VENDORS[0]))));

  bool staOk = false;
  if (strlen(gSettings.wifiSsid) > 0) {
    staOk = connectSta();
  }
  if (!staOk && !portalMode) {
    startPortal();
  }

  PN532Serial.begin(PN532_BAUD, SERIAL_8N1, PN532_RX_PIN, PN532_TX_PIN);

  if (!nfc.begin()) {
    nfcReady = false;
    Serial.println("[PN532] begin failed");
    debugPrint("[DEBUG] PN532 begin failed - check HSU mode, power, RX/TX wiring and reset pin");
  } else {
    uint32_t version = nfc.getFirmwareVersion();
    if (!version) {
      nfcReady = false;
      Serial.println("[PN532] not found");
      debugPrint("[DEBUG] PN532 firmware read failed");
    } else {
      nfc.SAMConfig();
      nfcReady = true;
      Serial.println("[PN532] ready");
      Serial.printf("[PN532] firmware IC=0x%02lX ver=%lu rev=%lu support=0x%02lX\n",
                    (version >> 24) & 0xFF,
                    (version >> 16) & 0xFF,
                    (version >> 8) & 0xFF,
                    version & 0xFF);
    }
  }

  lastPollMs = millis();
  lastTagSeenMs = 0;
  scheduleInitialPrinterQueries(millis());
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
  now = millis();

  if (gTagState.active && gTagState.lastSeenMs != 0 && (now - gTagState.lastSeenMs) > TAG_ACTIVE_WINDOW_MS) {
    gTagState.active = false;
    bumpStateRevision();
  }

  if (nfcReady && (now - lastPollMs) >= TAG_POLL_MS) {
    lastPollMs = now;

    uint8_t uid[10] = {0};
    uint8_t uidLen = 0;
    String openspoolJson;

    bool hasValidTag = readOpenSpoolFromCurrentTag(uid, uidLen, openspoolJson);
    if (hasValidTag) {
      handleValidMappedTagPayload("OpenSpool", openspoolJson, uid, uidLen);
      return;
    }

    String qidiJson;
    bool hasQidiTag = readQidiFromCurrentTag(uid, uidLen, qidiJson);
    if (hasQidiTag) {
      handleValidMappedTagPayload("QIDI", qidiJson, uid, uidLen);
      return;
    }
  } else if (!nfcReady) {
    delay(20);
  }

  processPendingSetVerification();
  processPrinterPolling();
}
