/*
  Working sketch: dev/U1_Argus_Remote_RFID_current.ino
  Release baseline: V1.1
  Created from: dev/U1_Argus_Remote_RFID_current.ino

  U1 Argus Remote RFID - V1.1
  ---------------------------------
  Target: ESP32-C3 Super Mini + PN532 (HSU/UART)

  Release features (V1.1):
  - AP provisioning + Web UI (when no Wi-Fi config or STA connect fails)
  - Stores config in Preferences (NVS)
  - mDNS hostname in STA mode
  - Reads NFC tags with PN532
  - Accepts only OpenSpool JSON payload (NDEF MIME: application/json, protocol=openspool)
  - Maps OpenSpool fields to Snapmaker U1 filament_detect/set API
  - Sends update webhook only when payload changed
  - Queries current printer channel state for the dashboard

  Required libraries:
  - ArduinoJson

  NOTE:
  - Web dashboard uses queried printer state plus last valid tag state.
  - Webhook dedupe still compares against last successful SENT payload.
*/

#include <WiFi.h>
#include <stdarg.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// ============================== Version ==============================
static const char* FW_NAME = "U1 Argus Remote RFID";
static const char* FW_VERSION = "V1.1";
static const char* FW_ITERATION = "V1.1";

// ============================== Debug ==============================
#define SERIAL_DEBUG 0
static const uint32_t SERIAL_BAUD = 115200;
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
static const int PN532_RX_PIN = 4;
static const int PN532_TX_PIN = 3;
static const uint32_t PN532_BAUD = 115200;

// ============================== Timing ==============================
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
static const uint32_t TAG_POLL_MS = 250;
static const uint32_t TAG_ACTIVE_WINDOW_MS = 1200;
static const uint32_t PRINTER_QUERY_MS = 3000;
static const uint16_t PN532_CMD_TIMEOUT_MS = 100;
static const uint16_t PN532_ACK_TIMEOUT_MS = 10;
static const uint8_t NTAG_FIRST_USER_PAGE = 4;
static const uint8_t NTAG_LAST_USER_PAGE = 129;
static const size_t NTAG_USER_BYTES = (NTAG_LAST_USER_PAGE - NTAG_FIRST_USER_PAGE + 1) * 4;
static const uint8_t NTAG_CC_PAGE = 3;

// ============================== Network defaults ==============================
static const char* AP_SSID = "U1-Argus-Setup";
static const char* AP_PASS = ""; // open AP as requested
static const uint16_t WEB_PORT = 80;
static const byte DNS_PORT = 53;
static const uint16_t DEFAULT_PRINTER_PORT = 7125;
static const uint8_t REMOTE_READER_COUNT = 3;

// ============================== Storage ==============================
Preferences prefs;
static const char* PREF_NS = "u1argus";

struct Settings {
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[33];
  char printerIp[40];
  uint16_t printerPort;
  uint8_t channel;
  char language[3];
  char remoteReaders[REMOTE_READER_COUNT][96];
};

Settings gSettings = {
  "", "", "u1-argus-rfid", "192.168.1.10", DEFAULT_PRINTER_PORT, 0, "en", {"", "", ""}
};

// ============================== Web ==============================
WebServer web(WEB_PORT);
DNSServer dnsServer;
bool portalMode = false;
bool portalRoutesReady = false;

// ============================== PN532 ==============================
HardwareSerial PN532Serial(1);
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
  int minTemp = -1;
  int maxTemp = -1;
  int bedTemp = -1;
  String openspoolJson;
  String mappedPayload;
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
uint32_t lastPrinterQueryMs = 0;
uint32_t stateRevision = 1;
String lastSentFingerprint = "";
String lastObservedFingerprint = "";
TagState gTagState;
PrinterChannelState gPrinterState;
WebhookState gWebhookState;

// ------------------------------ Helpers ------------------------------
static String configPageHtml(const String& msg = "");
static String dashboardPageHtml();
static bool tryParseIntField(JsonVariantConst v, int& out);
static String normalizedReaderUrl(const char* raw);
static bool uiLangIsDe();
static const char* tr(const char* en, const char* de);
static String langToggleHtml(const char* backPath);

#if SERIAL_DEBUG
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

static bool pn532GetLastUserPage(uint16_t& lastPageOut) {
  uint8_t cc[16] = {0};
  if (!pn532ReadPageWindow(NTAG_CC_PAGE, cc)) {
    debugPrint("[DEBUG] failed to read CC page via raw HSU");
    return false;
  }
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

static void pn532WakeupHsu() {
  const uint8_t wakeup[] = {0x55, 0x55, 0x00, 0x00, 0x00};
  PN532Serial.write(wakeup, sizeof(wakeup));
  PN532Serial.flush();
  delay(20);
  flushPn532Input();
}

static bool pn532GetFirmwareVersionRaw(uint32_t& versionOut) {
  uint8_t resp[8] = {0};
  size_t respLen = 0;
  if (!pn532Transact(0x02, nullptr, 0, 0x03, resp, sizeof(resp), respLen, 1000)) return false;
  if (respLen < 4) return false;
  versionOut = ((uint32_t)resp[0] << 24) |
               ((uint32_t)resp[1] << 16) |
               ((uint32_t)resp[2] << 8) |
               (uint32_t)resp[3];
  return true;
}

static bool pn532SamConfigRaw() {
  const uint8_t payload[] = {0x01, 0x14, 0x01};
  uint8_t resp[4] = {0};
  size_t respLen = 0;
  return pn532Transact(0x14, payload, sizeof(payload), 0x15, resp, sizeof(resp), respLen, 1000);
}

static void safeCopy(char* dst, size_t dstSize, const String& src) {
  if (!dst || dstSize == 0) return;
  size_t n = src.length();
  if (n >= dstSize) n = dstSize - 1;
  memcpy(dst, src.c_str(), n);
  dst[n] = '\0';
}

static String htmlEscape(const String& in) {
  String out = in;
  out.replace("&", "&amp;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  return out;
}

static bool parseIpPort(const char* ip, uint16_t port) {
  IPAddress tmp;
  if (!tmp.fromString(ip)) return false;
  if (port == 0) return false;
  return true;
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

static bool uiLangIsDe() {
  return strcmp(gSettings.language, "de") == 0;
}

static const char* tr(const char* en, const char* de) {
  return uiLangIsDe() ? de : en;
}

static String langToggleHtml(const char* backPath) {
  String body;
  body.reserve(320);
  const bool isDe = uiLangIsDe();
  body += F("<form method='GET' action='/lang' style='margin:0'><input type='hidden' name='back' value='");
  body += htmlEscape(backPath ? backPath : "/");
  body += F("'><input type='hidden' name='set' value='");
  body += isDe ? "en" : "de";
  body += F("'><button type='submit' class='langToggle' aria-label='Language'><span>EN</span><span class='langKnob");
  if (isDe) body += F(" is-de");
  body += F("'></span><span>DE</span></button></form>");
  return body;
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
    web.send(200, "text/html; charset=utf-8", portalMode ? configPageHtml() : dashboardPageHtml());
  }
}

// ------------------------------ Settings ------------------------------
static void loadSettings() {
  prefs.begin(PREF_NS, true);
  String ssid = prefs.getString("wifi_ssid", "");
  String pass = prefs.getString("wifi_pass", "");
  String host = prefs.getString("hostname", "u1-argus-rfid");
  String ip = prefs.getString("printer_ip", "192.168.1.10");
  uint16_t port = prefs.getUShort("printer_port", DEFAULT_PRINTER_PORT);
  uint8_t channel = prefs.getUChar("channel", 0);
  String language = prefs.getString("language", "en");
  String readerVals[REMOTE_READER_COUNT];
  for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
    String key = String("reader_") + String(i + 2);
    readerVals[i] = prefs.getString(key.c_str(), "");
  }
  prefs.end();

  safeCopy(gSettings.wifiSsid, sizeof(gSettings.wifiSsid), ssid);
  safeCopy(gSettings.wifiPass, sizeof(gSettings.wifiPass), pass);
  safeCopy(gSettings.hostname, sizeof(gSettings.hostname), host);
  safeCopy(gSettings.printerIp, sizeof(gSettings.printerIp), ip);
  gSettings.printerPort = port;
  gSettings.channel = (channel > 3) ? 0 : channel;
  safeCopy(gSettings.language, sizeof(gSettings.language), (language == "de") ? "de" : "en");
  for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
    safeCopy(gSettings.remoteReaders[i], sizeof(gSettings.remoteReaders[i]), readerVals[i]);
  }
}

static void saveSettings() {
  prefs.begin(PREF_NS, false);
  prefs.putString("wifi_ssid", gSettings.wifiSsid);
  prefs.putString("wifi_pass", gSettings.wifiPass);
  prefs.putString("hostname", gSettings.hostname);
  prefs.putString("printer_ip", gSettings.printerIp);
  prefs.putUShort("printer_port", gSettings.printerPort);
  prefs.putUChar("channel", gSettings.channel);
  prefs.putString("language", gSettings.language);
  for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
    String key = String("reader_") + String(i + 2);
    prefs.putString(key.c_str(), gSettings.remoteReaders[i]);
  }
  prefs.end();
}

static String filamentDetectUrl() {
  return String("http://") + gSettings.printerIp + ":" + String(gSettings.printerPort) + "/printer/filament_detect/set";
}

static String printerChannelQueryUrl() {
  return String("http://") + gSettings.printerIp + ":" + String(gSettings.printerPort) +
         "/printer/objects/query?filament_detect&filament_motion_sensor%20e0_filament&filament_motion_sensor%20e1_filament&filament_motion_sensor%20e2_filament&filament_motion_sensor%20e3_filament";
}

static void storeTagState(const String& openspoolJson,
                          const uint8_t* uid,
                          uint8_t uidLen,
                          const String& mappedPayload,
                          const String& fingerprint) {
  JsonDocument doc;
  deserializeJson(doc, openspoolJson);

  TagState next;
  next.hasData = true;
  next.active = true;
  next.lastSeenMs = millis();
  next.fingerprint = fingerprint;
  next.uidHex = bytesToHexString(uid, uidLen);
  next.vendor = doc["brand"].is<const char*>() ? String(doc["brand"].as<const char*>()) : "";
  next.mainType = doc["type"].is<const char*>() ? String(doc["type"].as<const char*>()) : "";
  next.subType = doc["subtype"].is<const char*>() ? String(doc["subtype"].as<const char*>()) : "";
  next.colorHex = doc["color_hex"].is<const char*>() ? String(doc["color_hex"].as<const char*>()) : "";
  int iv = 0;
  if (tryParseIntField(doc["min_temp"], iv)) next.minTemp = iv;
  if (tryParseIntField(doc["max_temp"], iv)) next.maxTemp = iv;
  if (tryParseIntField(doc["bed_min_temp"], iv)) {
    next.bedTemp = iv;
  } else if (tryParseIntField(doc["bed_max_temp"], iv)) {
    next.bedTemp = iv;
  }
  next.openspoolJson = openspoolJson;
  next.mappedPayload = mappedPayload;

  bool changed = !gTagState.hasData ||
                 !gTagState.active ||
                 gTagState.fingerprint != next.fingerprint ||
                 gTagState.openspoolJson != next.openspoolJson ||
                 gTagState.mappedPayload != next.mappedPayload;

  gTagState = next;
  if (changed) bumpStateRevision();
}

static void setPrinterStateDisconnected(const char* reason) {
  bool changed = gPrinterState.wifiConnected ||
                 gPrinterState.queryOk ||
                 gPrinterState.error != String(reason ? reason : "");
  gPrinterState.wifiConnected = false;
  gPrinterState.queryOk = false;
  gPrinterState.hasInfo = false;
  gPrinterState.motionKnown = false;
  gPrinterState.httpCode = 0;
  gPrinterState.error = reason ? reason : "";
  gPrinterState.endpoint = printerChannelQueryUrl();
  gPrinterState.lastQueryMs = millis();
  if (changed) bumpStateRevision();
}

static void updatePrinterStateFromJson(const String& rawResponse, int httpCode) {
  PrinterChannelState next;
  next.wifiConnected = true;
  next.httpCode = httpCode;
  next.lastQueryMs = millis();
  next.endpoint = printerChannelQueryUrl();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, rawResponse);
  if (err) {
    next.queryOk = false;
    next.error = "JSON decode failed";
  } else {
    JsonObject status = doc["result"]["status"];
    JsonArray infoArr = status["filament_detect"]["info"];
    if (!infoArr.isNull() && gSettings.channel < infoArr.size()) {
      JsonObject info = infoArr[gSettings.channel];
      next.queryOk = true;
      next.hasInfo = !info.isNull();
      next.vendor = info["VENDOR"].is<const char*>() ? String(info["VENDOR"].as<const char*>()) : "";
      next.manufacturer = info["MANUFACTURER"].is<const char*>() ? String(info["MANUFACTURER"].as<const char*>()) : "";
      next.mainType = info["MAIN_TYPE"].is<const char*>() ? String(info["MAIN_TYPE"].as<const char*>()) : "";
      next.subType = info["SUB_TYPE"].is<const char*>() ? String(info["SUB_TYPE"].as<const char*>()) : "";
      if (info["RGB_1"].is<int>() || info["RGB_1"].is<long>()) {
        next.colorHex = rgbIntToHexString(info["RGB_1"].as<int>());
      }
      if (info["HOTEND_MIN_TEMP"].is<int>() || info["HOTEND_MIN_TEMP"].is<long>()) next.minTemp = info["HOTEND_MIN_TEMP"].as<int>();
      if (info["HOTEND_MAX_TEMP"].is<int>() || info["HOTEND_MAX_TEMP"].is<long>()) next.maxTemp = info["HOTEND_MAX_TEMP"].as<int>();
      if (info["BED_TEMP"].is<int>() || info["BED_TEMP"].is<long>()) next.bedTemp = info["BED_TEMP"].as<int>();
      if (info["OFFICIAL"].is<bool>()) {
        next.officialKnown = true;
        next.official = info["OFFICIAL"].as<bool>();
      }
      serializeJson(info, next.rawJson);
    } else {
      next.queryOk = false;
      next.error = "filament_detect.info missing";
    }

    String sensorKey = String("filament_motion_sensor e") + String(gSettings.channel) + "_filament";
    JsonVariant sensor = status[sensorKey];
    if (!sensor.isNull() && sensor["filament_detected"].is<bool>()) {
      next.motionKnown = true;
      next.filamentDetected = sensor["filament_detected"].as<bool>();
    }
  }

  String fingerprint;
  fingerprint.reserve(256);
  fingerprint += next.vendor;
  fingerprint += '|';
  fingerprint += next.manufacturer;
  fingerprint += '|';
  fingerprint += next.mainType;
  fingerprint += '|';
  fingerprint += next.subType;
  fingerprint += '|';
  fingerprint += next.colorHex;
  fingerprint += '|';
  fingerprint += String(next.minTemp);
  fingerprint += '|';
  fingerprint += String(next.maxTemp);
  fingerprint += '|';
  fingerprint += String(next.bedTemp);
  fingerprint += '|';
  fingerprint += String(next.motionKnown ? (next.filamentDetected ? 1 : 0) : -1);
  fingerprint += '|';
  fingerprint += String(next.queryOk ? 1 : 0);
  fingerprint += '|';
  fingerprint += next.error;
  next.fingerprint = fingerprint;

  bool changed = gPrinterState.fingerprint != next.fingerprint ||
                 gPrinterState.queryOk != next.queryOk ||
                 gPrinterState.httpCode != next.httpCode;
  gPrinterState = next;
  if (changed) bumpStateRevision();
}

static bool fetchPrinterChannelState() {
  if (WiFi.status() != WL_CONNECTED) {
    setPrinterStateDisconnected("Wi-Fi disconnected");
    return false;
  }

  String url = printerChannelQueryUrl();
  debugPrintf("[DEBUG] PRINTER QUERY %s\n", url.c_str());

  HTTPClient http;
  http.setTimeout(2500);
  if (!http.begin(url)) {
    setPrinterStateDisconnected("HTTP begin failed");
    return false;
  }

  int httpCode = http.GET();
  String resp = (httpCode > 0) ? http.getString() : "";
  http.end();

  debugPrintf("[DEBUG] PRINTER QUERY RESULT ok=%d code=%d body=%s\n",
              httpCode > 0 ? 1 : 0,
              httpCode,
              resp.c_str());

  if (httpCode <= 0) {
    PrinterChannelState next = gPrinterState;
    next.wifiConnected = true;
    next.queryOk = false;
    next.hasInfo = false;
    next.httpCode = httpCode;
    next.error = "HTTP GET failed";
    next.endpoint = url;
    next.lastQueryMs = millis();
    next.fingerprint = String("err|") + String(httpCode);
    bool changed = gPrinterState.fingerprint != next.fingerprint || gPrinterState.httpCode != next.httpCode;
    gPrinterState = next;
    if (changed) bumpStateRevision();
    return false;
  }

  updatePrinterStateFromJson(resp, httpCode);
  return gPrinterState.queryOk;
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
  printer["port"] = gSettings.printerPort;
  printer["endpoint"] = gPrinterState.endpoint;
  printer["query_ok"] = gPrinterState.queryOk;
  printer["age_ms"] = gPrinterState.lastQueryMs ? (millis() - gPrinterState.lastQueryMs) : 0;
  printer["vendor"] = gPrinterState.vendor;
  printer["manufacturer"] = gPrinterState.manufacturer;
  printer["main_type"] = gPrinterState.mainType;
  printer["sub_type"] = gPrinterState.subType;
  printer["color_hex"] = gPrinterState.colorHex;
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
  body.reserve(3600);
  body += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  body += F("<title>U1 Argus RFID Setup</title><style>:root{--bg:#f4efe5;--panel:#fffaf1;--ink:#18231e;--muted:#5d6a62;--line:#d8cdb7;--accent:#b85c38;--accent2:#2f5d50}*{box-sizing:border-box}body{font-family:'Trebuchet MS',Verdana,sans-serif;max-width:820px;margin:0 auto;padding:18px 14px 48px;background:radial-gradient(circle at top,#fff9ee 0,#f4efe5 48%,#ece3d3 100%);color:var(--ink)}.shell{background:var(--panel);border:1px solid var(--line);border-radius:22px;padding:22px;box-shadow:0 18px 44px rgba(67,49,26,.10)}label{display:block;margin-top:12px;font-weight:700}input,select{width:100%;padding:12px 13px;margin-top:5px;border:1px solid #cdbfa6;border-radius:12px;background:#fffdf8}button,.btn{display:inline-block;margin-top:16px;padding:11px 15px;border:none;border-radius:999px;background:var(--accent);color:#fff;text-decoration:none;font-weight:700}.btn.alt{background:var(--accent2)}small,.muted{color:var(--muted)}code{background:#efe4d0;padding:2px 6px;border-radius:6px}.top{display:flex;justify-content:space-between;gap:12px;align-items:center;flex-wrap:wrap}.topRight{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.msg{padding:12px 14px;border-radius:12px;background:#f7e9d4;border:1px solid #e7d0ac;margin:14px 0}.group{margin-top:18px;padding-top:8px;border-top:1px dashed #d8cdb7}.langToggle{margin-top:0;background:#efe4d0;color:#21312b;padding:6px 10px;display:inline-flex;align-items:center;gap:8px;font-weight:800}.langKnob{display:inline-block;width:24px;height:24px;border-radius:999px;background:var(--accent2);box-shadow:inset 0 0 0 2px rgba(255,255,255,.25)}.langKnob.is-de{background:var(--accent)}</style></head><body>");
  body += F("<div class='shell'>");
  body += F("<div class='top'><div>");
  body += tr("<h2>U1 Argus Remote RFID - Setup</h2>", "<h2>U1 Argus Remote RFID - Einrichtung</h2>");
  body += F("<p><small>");
  body += tr("Firmware: ", "Firmware: ");
  body += FW_VERSION;
  body += F("</small></p></div>");
  body += F("<div class='topRight'>");
  body += langToggleHtml(portalMode ? "/" : "/setup");
  if (!portalMode) body += String(F("<a class='btn alt' href='/'>")) + tr("Dashboard", "Dashboard") + F("</a>");
  body += F("</div></div>");
  if (msg.length()) {
    body += F("<div class='msg'><b>");
    body += htmlEscape(msg);
    body += F("</b></div>");
  }
  body += F("<form method='POST' action='/save'>");
  body += String(F("<label>")) + tr("Wi-Fi SSID", "WLAN-SSID") + F("</label><input name='ssid' maxlength='32' required value='"); body += htmlEscape(gSettings.wifiSsid); body += F("'>");
  body += String(F("<label>")) + tr("Wi-Fi Password", "WLAN-Passwort") + F("</label><input name='pass' maxlength='64' value='"); body += htmlEscape(gSettings.wifiPass); body += F("'>");
  body += String(F("<label>")) + tr("Hostname (mDNS, no .local)", "Hostname (mDNS, ohne .local)") + F("</label><input name='hostname' maxlength='32' required value='"); body += htmlEscape(gSettings.hostname); body += F("'>");
  body += String(F("<label>")) + tr("Snapmaker U1 IP", "Snapmaker U1 IP") + F("</label><input name='printer_ip' maxlength='39' required value='"); body += htmlEscape(gSettings.printerIp); body += F("'>");
  body += String(F("<label>")) + tr("Snapmaker U1 Port", "Snapmaker U1 Port") + F("</label><input name='printer_port' type='number' min='1' max='65535' required value='"); body += String(gSettings.printerPort); body += F("'>");
  body += String(F("<label>")) + tr("Channel (Tool Head)", "Channel (Toolhead)") + F("</label><select name='channel'>");
  for (int i = 0; i < 4; i++) {
    body += "<option value='" + String(i) + "'" + String(gSettings.channel == i ? " selected" : "") + ">" + String(i) + "</option>";
  }
  body += F("</select>");
  body += String(F("<div class='group'><p><b>")) + tr("Additional U1 Argus Remote Readers", "Weitere U1 Argus Remote Reader") + F("</b><br><small>") + tr("Optional: IP or full URL of up to three more tag readers for the dashboard.", "Optional: IP oder komplette URL der drei weiteren Tagreader fuer das Dashboard.") + F("</small></p>");
  for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
    body += String(F("<label>")) + tr("Reader ", "Reader ");
    body += String(i + 2);
    body += F("</label><input name='reader_");
    body += String(i + 2);
    body += F("' maxlength='95' placeholder='");
    body += tr("e.g. 192.168.1.51 or http://u1-argus-2.local/", "z. B. 192.168.1.51 oder http://u1-argus-2.local/");
    body += F("' value='");
    body += htmlEscape(gSettings.remoteReaders[i]);
    body += F("'>");
  }
  body += F("</div>");
  body += String(F("<button type='submit'>")) + tr("Save & Reboot", "Speichern & Neustart") + F("</button></form>");
  body += String(F("<p><small>")) + tr("API target: ", "API-Ziel: ") + F("<code>/printer/filament_detect/set</code></small></p>");
  body += F("</div>");
  body += F("</body></html>");
  return body;
}

static String dashboardPageHtml() {
  String body;
  body.reserve(7800);
  body += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  body += F("<title>U1 Argus RFID Status</title><style>:root{--bg:#f6f1e6;--panel:#fffaf2;--ink:#1a241f;--muted:#617068;--line:#d9cdb8;--accent:#b65f3b;--accent2:#2f5d50;--ok:#3f7d4f;--warn:#c2862a;--bad:#b64242}*{box-sizing:border-box}body{margin:0;font-family:'Trebuchet MS',Verdana,sans-serif;background:radial-gradient(circle at top,#fffaf0 0,#f1eadc 44%,#e8decf 100%);color:var(--ink)}main{max-width:1080px;margin:0 auto;padding:18px 14px 40px}.hero{display:grid;grid-template-columns:1.3fr .7fr;gap:14px}.panel{background:var(--panel);border:1px solid var(--line);border-radius:24px;padding:18px;box-shadow:0 18px 44px rgba(67,49,26,.10)}h1,h2,h3,p{margin:0}h1{font-size:1.9rem;line-height:1.05}.sub{margin-top:10px;color:var(--muted)}.meta{display:flex;gap:10px;flex-wrap:wrap;margin-top:16px}.chip{display:inline-flex;align-items:center;gap:8px;padding:8px 12px;border-radius:999px;background:#efe4d0;color:#2b312d;font-weight:700;font-size:.92rem}.dot{width:10px;height:10px;border-radius:50%;background:#bbb}.ok{background:var(--ok)}.warn{background:var(--warn)}.bad{background:var(--bad)}.actions{display:flex;justify-content:flex-start;align-items:flex-start}.readerNav{display:flex;flex-wrap:wrap;gap:10px}.btn{display:inline-block;padding:11px 15px;border-radius:999px;background:var(--accent2);color:#fff;text-decoration:none;font-weight:700}.btn.secondary{background:var(--accent)}.btn.ghost{background:#e9dcc7;color:#30463d}.langToggle{margin-top:0;background:#efe4d0;color:#21312b;padding:6px 10px;display:inline-flex;align-items:center;gap:8px;font-weight:800;text-decoration:none}.langKnob{display:inline-block;width:24px;height:24px;border-radius:999px;background:var(--accent2);box-shadow:inset 0 0 0 2px rgba(255,255,255,.25)}.langKnob.is-de{background:var(--accent)}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px;margin-top:14px}.cardTitle{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:14px}.stamp{font-size:.88rem;color:var(--muted)}.kv{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px 14px}.item{padding:10px 12px;border-radius:16px;background:#f6efe2;border:1px solid #e7dbc7}.label{font-size:.78rem;color:var(--muted);text-transform:uppercase;letter-spacing:.05em}.value{margin-top:4px;font-size:1rem;font-weight:700;word-break:break-word}.accent{color:var(--accent2)}.swatch{display:inline-flex;align-items:center;gap:8px}.sw{width:16px;height:16px;border-radius:50%;border:1px solid rgba(0,0,0,.15);background:#ddd}.json{margin-top:14px;padding:14px;border-radius:18px;background:#171c19;color:#d7efe5;white-space:pre-wrap;word-break:break-word;font-family:Consolas,'Courier New',monospace;font-size:.84rem;max-height:240px;overflow:auto}.foot{margin-top:14px;color:var(--muted);font-size:.9rem}@media (max-width:860px){.hero,.grid,.kv{grid-template-columns:1fr}.actions{justify-content:flex-start}}</style></head><body><main>");
  body += String(F("<section class='hero'><div class='panel'><h1>U1 Argus Remote RFID</h1><p class='sub'>")) + tr("Live status for the printer channel and the last OpenSpool tag that was read. The page only refreshes when the state actually changes.", "Live-Status fuer Drucker-Channel und den zuletzt gelesenen OpenSpool-Tag. Die Seite zieht nur neue Daten, wenn sich der Zustand wirklich geaendert hat.") + F("</p><div class='meta'><span class='chip'><span class='dot warn' id='wifiDot'></span><span id='wifiText'>") + tr("Checking Wi-Fi", "WLAN wird geprueft") + F("</span></span><span class='chip'><span class='dot warn' id='printerDot'></span><span id='printerText'>") + tr("Loading printer status", "Druckerstatus wird geladen") + F("</span></span><span class='chip'><span class='dot warn' id='tagDot'></span><span id='tagText'>") + tr("Loading tag status", "Tag-Status wird geladen") + F("</span></span></div></div><div class='panel actions'><div class='readerNav'>");
  body += langToggleHtml("/");
  body += String(F("<a class='btn' href='/setup'>")) + tr("Setup", "Setup") + F("</a><a class='btn secondary' href='/'>") + tr("This Reader", "Dieser Reader") + F("</a>");
  for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
    String remoteUrl = normalizedReaderUrl(gSettings.remoteReaders[i]);
    if (!remoteUrl.length()) continue;
    body += F("<a class='btn ghost' href='");
    body += htmlEscape(remoteUrl);
    body += F("'>");
    body += tr("Reader ", "Reader ");
    body += String(i + 2);
    body += F("</a>");
  }
  body += F("</div></div></section>");
  body += String(F("<section class='grid'><article class='panel'><div class='cardTitle'><div><h2>")) + tr("Printer Channel", "Printer Channel") + F("</h2><p class='stamp' id='printerStamp'>") + tr("No response yet", "Noch keine Antwort") + F("</p></div><span class='chip'>") + tr("Channel", "Channel") + F(" <span id='channelId'>-</span></span></div><div class='kv' id='printerKv'></div><div class='json' id='printerJson'>") + tr("No printer data yet.", "Noch keine Druckerdaten.") + F("</div></article><article class='panel'><div class='cardTitle'><div><h2>") + tr("Tag Reader", "Tag Reader") + F("</h2><p class='stamp' id='tagStamp'>") + tr("No valid tag read yet", "Noch kein gueltiger Tag gelesen") + F("</p></div><span class='chip' id='tagBadge'>") + tr("no tag", "kein Tag") + F("</span></div><div class='kv' id='tagKv'></div><div class='json' id='tagJson'>") + tr("No tag data yet.", "Noch keine Tag-Daten.") + F("</div></article></section>");
  body += String(F("<section class='grid'><article class='panel'><div class='cardTitle'><div><h2>Webhook</h2><p class='stamp' id='hookStamp'>")) + tr("Nothing sent yet", "Noch nichts gesendet") + F("</p></div><span class='chip' id='hookBadge'>idle</span></div><div class='kv' id='hookKv'></div><div class='json' id='hookJson'>") + tr("No payload sent yet.", "Kein Payload gesendet.") + F("</div></article><article class='panel'><div class='cardTitle'><div><h2>") + tr("Network", "Netzwerk") + F("</h2><p class='stamp'>ESP32-C3 ") + tr("status", "Status") + F("</p></div><span class='chip accent' id='revBadge'>rev -</span></div><div class='kv' id='netKv'></div><p class='foot'>Firmware ");
  body += FW_VERSION;
  body += F(" / ");
  body += FW_ITERATION;
  body += F("</p></article></section>");
  body += F("<script>");
  body += String(F("const LANG='")) + (uiLangIsDe() ? "de" : "en") + F("';");
  body += F("const T=LANG==='de'?{wifiChecking:'WLAN wird geprueft',wifiConnected:'WLAN ',wifiDisconnected:'WLAN getrennt',printerLoading:'Druckerstatus wird geladen',printerResponding:'Drucker antwortet',printerNoStatus:'Kein Druckerstatus',tagLoading:'Tag-Status wird geladen',tagActive:'Tag aktiv erkannt',tagStored:'letzter Tag gespeichert',tagNone:'kein Tag',noResponse:'Noch keine Antwort',updatedAgo:'aktualisiert vor ',statusError:'Statusfehler: ',unknown:'unbekannt',vendor:'Vendor',manufacturer:'Manufacturer',material:'Material',subType:'Sub Type',color:'Farbe',nozzle:'Nozzle',bed:'Bed',filamentSensor:'Filament Sensor',official:'Official',filamentYes:'Filament erkannt',filamentNo:'kein Filament',yes:'ja',no:'nein',noPrinterData:'Noch keine Druckerdaten.',lastValidTag:'letztes gueltiges Tag vor ',noValidTag:'Noch kein gueltiger Tag gelesen',ready:'bereit',stored:'gespeichert',noTagData:'Noch keine Tag-Daten.',payloadStatus:'Payload Status',lastSendTry:'letzter Sendeversuch vor ',nothingSent:'Noch nichts gesendet',successful:'erfolgreich',failed:'fehlgeschlagen',result:'Ergebnis',target:'Target',channel:'Channel',httpCode:'HTTP Code',idle:'idle',errorShort:'fehler',noPayload:'Kein Payload gesendet.',ssid:'SSID',ip:'IP',hostname:'Hostname',rssi:'RSSI',mode:'Mode',printerPort:'Printer Port',webUpdateError:'Web-Update Fehler',seconds:' s',minutes:' min',hours:' h'}:{wifiChecking:'Checking Wi-Fi',wifiConnected:'Wi-Fi ',wifiDisconnected:'Wi-Fi disconnected',printerLoading:'Loading printer status',printerResponding:'Printer responding',printerNoStatus:'No printer status',tagLoading:'Loading tag status',tagActive:'Tag actively detected',tagStored:'last tag stored',tagNone:'no tag',noResponse:'No response yet',updatedAgo:'updated ',statusError:'Status error: ',unknown:'unknown',vendor:'Vendor',manufacturer:'Manufacturer',material:'Material',subType:'Sub Type',color:'Color',nozzle:'Nozzle',bed:'Bed',filamentSensor:'Filament Sensor',official:'Official',filamentYes:'Filament detected',filamentNo:'No filament',yes:'yes',no:'no',noPrinterData:'No printer data yet.',lastValidTag:'last valid tag ',noValidTag:'No valid tag read yet',ready:'ready',stored:'stored',noTagData:'No tag data yet.',payloadStatus:'Payload Status',lastSendTry:'last send attempt ',nothingSent:'Nothing sent yet',successful:'successful',failed:'failed',result:'Result',target:'Target',channel:'Channel',httpCode:'HTTP Code',idle:'idle',errorShort:'error',noPayload:'No payload sent yet.',ssid:'SSID',ip:'IP',hostname:'Hostname',rssi:'RSSI',mode:'Mode',printerPort:'Printer Port',webUpdateError:'Web update error',seconds:' s',minutes:' min',hours:' h'};");
  body += F("let etag='';const q=s=>document.querySelector(s);const esc=s=>String(s??'').replace(/[&<>\"]/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[m]));const age=ms=>{if(ms==null)return'-';const s=Math.max(0,Math.round(ms/1000));if(s<60)return s+T.seconds;const m=Math.floor(s/60);if(m<60)return m+T.minutes;const h=Math.floor(m/60);return h+T.hours;};const kv=(rows)=>rows.map(r=>`<div class='item'><div class='label'>${esc(r[0])}</div><div class='value'>${r[1]}</div></div>`).join('');const sw=(hex)=>hex?`<span class='swatch'><span class='sw' style='background:${esc(hex)}'></span>${esc(hex)}</span>`:'-';const badge=(dotSel,textSel,ok,warn,text)=>{const dot=q(dotSel);dot.className='dot '+(ok?'ok':warn?'warn':'bad');q(textSel).textContent=text;};function render(d){q('#revBadge').textContent='rev '+d.revision;badge('#wifiDot','#wifiText',d.wifi.connected,!d.wifi.connected,d.wifi.connected?(T.wifiConnected+(d.wifi.ip||'')):T.wifiDisconnected);badge('#printerDot','#printerText',d.printer.query_ok,!d.printer.query_ok,d.printer.query_ok?T.printerResponding:(d.printer.error||T.printerNoStatus));badge('#tagDot','#tagText',d.tag.active,d.tag.has_data,d.tag.active?T.tagActive:(d.tag.has_data?T.tagStored:T.tagNone));q('#channelId').textContent=d.printer.channel;q('#printerStamp').textContent=d.printer.query_ok?(T.updatedAgo+age(d.printer.age_ms)):(T.statusError+(d.printer.error||T.unknown));q('#printerKv').innerHTML=kv([[T.vendor,esc(d.printer.vendor||'-')],[T.manufacturer,esc(d.printer.manufacturer||'-')],[T.material,esc(d.printer.main_type||'-')],[T.subType,esc(d.printer.sub_type||'-')],[T.color,sw(d.printer.color_hex)],[T.nozzle,esc(d.printer.min_temp!=null&&d.printer.max_temp!=null?`${d.printer.min_temp} - ${d.printer.max_temp} C`:'-')],[T.bed,esc(d.printer.bed_temp!=null?`${d.printer.bed_temp} C`:'-')],[T.filamentSensor,esc(d.printer.filament_detected==null?'-':(d.printer.filament_detected?T.filamentYes:T.filamentNo))],[T.official,esc(d.printer.official==null?'-':(d.printer.official?T.yes:T.no))]]);q('#printerJson').textContent=d.printer.raw_json||T.noPrinterData;q('#tagStamp').textContent=d.tag.has_data?(T.lastValidTag+age(d.tag.age_ms)):T.noValidTag;q('#tagBadge').textContent=d.tag.active?T.ready:T.tagStored;q('#tagKv').innerHTML=kv([['UID',esc(d.tag.uid||'-')],[T.vendor,esc(d.tag.vendor||'-')],[T.material,esc(d.tag.main_type||'-')],[T.subType,esc(d.tag.sub_type||'-')],[T.color,sw(d.tag.color_hex)],[T.nozzle,esc(d.tag.min_temp!=null&&d.tag.max_temp!=null?`${d.tag.min_temp} - ${d.tag.max_temp} C`:'-')],[T.bed,esc(d.tag.bed_temp!=null?`${d.tag.bed_temp} C`:'-')],[T.payloadStatus,esc(d.tag.active?T.ready:T.stored)]]);q('#tagJson').textContent=d.tag.openspool_json||T.noTagData;q('#hookStamp').textContent=d.webhook.known?(T.lastSendTry+age(d.webhook.age_ms)):T.nothingSent;q('#hookBadge').textContent=d.webhook.known?(d.webhook.ok?'ok':T.errorShort):T.idle;q('#hookKv').innerHTML=kv([[T.httpCode,esc(d.webhook.http_code??'-')],[T.result,esc(d.webhook.known?(d.webhook.ok?T.successful:T.failed):'-')],[T.target,esc(d.printer.endpoint||'-')],[T.channel,esc(String(d.printer.channel))]]);q('#hookJson').textContent=d.tag.mapped_payload||d.webhook.response||T.noPayload;q('#netKv').innerHTML=kv([[T.ssid,esc(d.wifi.ssid||'-')],[T.ip,esc(d.wifi.ip||'-')],[T.hostname,esc(d.wifi.hostname||'-')],[T.rssi,esc(d.wifi.rssi==null?'-':`${d.wifi.rssi} dBm`)],[T.mode,esc(d.mode||'-')],[T.printerPort,esc(String(d.printer.port||'-'))]]);}async function refresh(){try{const h={};if(etag)h['If-None-Match']=etag;const r=await fetch('/api/state',{cache:'no-store',headers:h});if(r.status===304)return;if(!r.ok)throw new Error('HTTP '+r.status);etag=r.headers.get('ETag')||etag;render(await r.json());}catch(e){q('#printerText').textContent=T.webUpdateError;q('#printerDot').className='dot bad';}}refresh();setInterval(refresh,2000);</script>");
  body += F("</main></body></html>");
  return body;
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
    web.send(200, "text/html; charset=utf-8", dashboardPageHtml());
  });
  web.on("/setup", HTTP_GET, []() {
    web.send(200, "text/html; charset=utf-8", configPageHtml());
  });
  web.on("/lang", HTTP_GET, []() {
    String set = web.arg("set");
    String back = web.arg("back");
    back.trim();
    if (back.length() == 0) back = "/";
    safeCopy(gSettings.language, sizeof(gSettings.language), (set == "de") ? "de" : "en");
    saveSettings();
    web.sendHeader("Location", back, true);
    web.send(302, "text/plain", "");
  });
  web.on("/api/state", HTTP_GET, []() {
    handleStateApi();
  });

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
    String portStr = web.arg("printer_port"); portStr.trim();
    String channelStr = web.arg("channel"); channelStr.trim();
    String readerUrls[REMOTE_READER_COUNT];
    for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
      readerUrls[i] = web.arg(String("reader_") + String(i + 2));
      readerUrls[i].trim();
    }

    uint32_t portVal = portStr.toInt();
    int ch = channelStr.toInt();

    if (ssid.length() == 0) {
      web.send(400, "text/html; charset=utf-8", configPageHtml(tr("SSID must not be empty.", "SSID darf nicht leer sein.")));
      return;
    }
    if (hostname.length() == 0) {
      web.send(400, "text/html; charset=utf-8", configPageHtml(tr("Hostname must not be empty.", "Hostname darf nicht leer sein.")));
      return;
    }
    if (ch < 0 || ch > 3) {
      web.send(400, "text/html; charset=utf-8", configPageHtml(tr("Channel must be 0..3.", "Channel muss 0..3 sein.")));
      return;
    }
    if (!parseIpPort(printerIp.c_str(), (uint16_t)portVal)) {
      web.send(400, "text/html; charset=utf-8", configPageHtml(tr("Printer IP/port is invalid.", "Printer IP/Port ist ungueltig.")));
      return;
    }

    safeCopy(gSettings.wifiSsid, sizeof(gSettings.wifiSsid), ssid);
    safeCopy(gSettings.wifiPass, sizeof(gSettings.wifiPass), pass);
    safeCopy(gSettings.hostname, sizeof(gSettings.hostname), hostname);
    safeCopy(gSettings.printerIp, sizeof(gSettings.printerIp), printerIp);
    gSettings.printerPort = (uint16_t)portVal;
    gSettings.channel = (uint8_t)ch;
    for (uint8_t i = 0; i < REMOTE_READER_COUNT; i++) {
      safeCopy(gSettings.remoteReaders[i], sizeof(gSettings.remoteReaders[i]), readerUrls[i]);
    }

    saveSettings();

    String ok = String(F("<html><body><h3>")) + tr("Saved. Rebooting...", "Gespeichert. Neustart...") + F("</h3></body></html>");
    web.send(200, "text/html; charset=utf-8", ok);
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

static void startPortal() {
  portalMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  setPrinterStateDisconnected("Setup portal active");

  setupPortalRoutes();
  dnsServer.stop();
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  web.begin();

  IPAddress apIp = WiFi.softAPIP();
  Serial.printf("[PORTAL] AP started: SSID=%s, IP=%s\n", AP_SSID, apIp.toString().c_str());
  debugPrintf("[DEBUG] Setup portal active, captive DNS on %s:%u\n",
              apIp.toString().c_str(),
              DNS_PORT);
}

// ------------------------------ STA mode ------------------------------
static bool connectSta() {
  if (strlen(gSettings.wifiSsid) == 0) return false;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(gSettings.wifiSsid, gSettings.wifiPass);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(200);
    Serial.print('.');
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WIFI] Connection failed");
    return false;
  }

  Serial.printf("\n[WIFI] Connected: %s\n", WiFi.localIP().toString().c_str());
  debugPrintf("[DEBUG] WiFi connected, hostname=%s, target=%s:%u\n",
              gSettings.hostname,
              gSettings.printerIp,
              gSettings.printerPort);

  if (strlen(gSettings.hostname) > 0) {
    if (MDNS.begin(gSettings.hostname)) {
      Serial.printf("[MDNS] http://%s.local/\n", gSettings.hostname);
    } else {
      Serial.println("[MDNS] start failed");
    }
  }

  // Start status endpoint in STA mode too (read-only)
  setupPortalRoutes();
  dnsServer.stop();
  web.begin();
  portalMode = false;
  gPrinterState.wifiConnected = true;

  return true;
}

// ------------------------------ NFC/OpenSpool parsing ------------------------------
static bool readOpenSpoolUserArea(uint8_t* out, size_t outLen, size_t& actualLen) {
  if (outLen < NTAG_USER_BYTES) return false;
  memset(out, 0x00, outLen);
  actualLen = 0;
  uint16_t lastPage = 0;
  if (!pn532GetLastUserPage(lastPage)) {
    debugPrint("[DEBUG] cannot determine user memory size");
    return false;
  }

  uint8_t data16[16] = {0};
  for (uint16_t page = NTAG_FIRST_USER_PAGE; page <= lastPage; page += 4) {
    if (!pn532ReadPageWindow((uint8_t)page, data16)) {
      debugPrintf("[DEBUG] raw HSU read error at page=%u\n", (unsigned)page);
      return false;
    }

    for (uint8_t offset = 0; offset < 4; ++offset) {
      const uint16_t thisPage = (uint16_t)(page + offset);
      if (thisPage > lastPage) break;
      const size_t dst = (size_t)(thisPage - NTAG_FIRST_USER_PAGE) * 4u;
      out[dst + 0] = data16[offset * 4 + 0];
      out[dst + 1] = data16[offset * 4 + 1];
      out[dst + 2] = data16[offset * 4 + 2];
      out[dst + 3] = data16[offset * 4 + 3];
      actualLen = dst + 4;
    }

    size_t ndefOffset = 0, ndefLen = 0;
    if (findNdefTlv(out, actualLen, ndefOffset, ndefLen)) {
      size_t needed = ndefOffset + ndefLen + 1;
      debugPrintf("[DEBUG] NDEF candidate found at offset=%u len=%u after page=%u\n",
                  (unsigned)ndefOffset,
                  (unsigned)ndefLen,
                  (unsigned)page);
      if (actualLen >= needed) {
        debugPrintf("[DEBUG] Completed raw HSU read at page=%u total=%u bytes\n",
                    (unsigned)page,
                    (unsigned)actualLen);
        return true;
      }
    }
  }
  return true;
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

static bool buildFilamentDetectPayload(const String& openspoolJson,
                                       const uint8_t* uid,
                                       uint8_t uidLen,
                                       String& outPayload,
                                       String& outFingerprint) {
  JsonDocument src;
  DeserializationError err = deserializeJson(src, openspoolJson);
  if (err) return false;

  if (!src["protocol"].is<const char*>()) return false;
  if (String(src["protocol"].as<const char*>()) != "openspool") return false;

  JsonDocument req;
  req["channel"] = gSettings.channel;
  JsonObject info = req["info"].to<JsonObject>();

  if (src["brand"].is<const char*>()) info["VENDOR"] = src["brand"].as<const char*>();
  if (src["type"].is<const char*>()) info["MAIN_TYPE"] = src["type"].as<const char*>();
  if (src["subtype"].is<const char*>()) info["SUB_TYPE"] = src["subtype"].as<const char*>();

  if (src["color_hex"].is<const char*>()) {
    int rgb = parseHexColorToRgbInt(src["color_hex"].as<const char*>());
    if (rgb >= 0) info["RGB_1"] = rgb;
  }

  int iv = 0;
  if (tryParseIntField(src["alpha"], iv)) info["ALPHA"] = constrain(iv, 0, 255);
  if (tryParseIntField(src["min_temp"], iv)) info["HOTEND_MIN_TEMP"] = iv;
  if (tryParseIntField(src["max_temp"], iv)) info["HOTEND_MAX_TEMP"] = iv;

  // BED_TEMP: prefer bed_min_temp, fallback bed_max_temp
  if (tryParseIntField(src["bed_min_temp"], iv)) {
    info["BED_TEMP"] = iv;
  } else if (tryParseIntField(src["bed_max_temp"], iv)) {
    info["BED_TEMP"] = iv;
  }

  JsonArray uidArr = info["CARD_UID"].to<JsonArray>();
  for (uint8_t i = 0; i < uidLen; i++) uidArr.add((int)uid[i]);

  outPayload = "";
  serializeJson(req, outPayload);

  // Use canonical-ish fingerprint for dedupe
  JsonDocument fp;
  deserializeJson(fp, outPayload);
  outFingerprint = "";
  serializeJson(fp, outFingerprint);

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

static void handleValidOpenSpoolPayload(const String& openspoolJson, const uint8_t* uid, uint8_t uidLen) {
  String payload;
  String fingerprint;
  if (!buildFilamentDetectPayload(openspoolJson, uid, uidLen, payload, fingerprint)) {
    Serial.println("[NFC] OpenSpool parse failed");
    debugPrintf("[DEBUG] NFC payload parse failed for UID=%s\n", bytesToHexString(uid, uidLen).c_str());
    pulseTagLedError();
    return;
  }

  lastTagSeenMs = millis();
  storeTagState(openspoolJson, uid, uidLen, payload, fingerprint);

  if (fingerprint != lastObservedFingerprint) {
    lastObservedFingerprint = fingerprint;
    debugPrintf("[DEBUG] NFC UID %s\n", bytesToHexString(uid, uidLen).c_str());
    debugPrintf("[DEBUG] NFC OpenSpool JSON %s\n", openspoolJson.c_str());
    debugPrintf("[DEBUG] NFC mapped payload %s\n", payload.c_str());
  }

  if (fingerprint == lastSentFingerprint) {
    debugPrint("[DEBUG] Payload unchanged, webhook skipped");
    return; // unchanged
  }

  int code = 0;
  String resp;
  bool ok = postJson(filamentDetectUrl(), payload, code, resp);
  bool hookOk = ok && code >= 200 && code < 300;
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

  Serial.printf("[API] SET sent=%d code=%d payload=%s\n", ok ? 1 : 0, code, payload.c_str());

  if (ok && code >= 200 && code < 300) {
    lastSentFingerprint = fingerprint;
  } else {
    pulseTagLedError();
  }
}

// ------------------------------ Tag polling ------------------------------
static bool readOpenSpoolFromCurrentTag(uint8_t* uid, uint8_t& uidLen, String& outJson) {
  uidLen = 0;
  if (!pn532InListPassiveTarget(uid, uidLen, 50)) {
    return false;
  }

  pulseTagLed();
  debugPrintf("[DEBUG] NFC tag detected, UID=%s\n", bytesToHexString(uid, uidLen).c_str());
  delay(10);

  static uint8_t buf[NTAG_USER_BYTES];
  size_t bufLen = 0;
  if (!readOpenSpoolUserArea(buf, sizeof(buf), bufLen)) {
    debugPrint("[DEBUG] NTAG user area read failed");
    pulseTagLedError();
    return false;
  }

  size_t ndefOffset = 0, ndefLen = 0;
  if (!findNdefTlv(buf, bufLen, ndefOffset, ndefLen)) {
    debugPrint("[DEBUG] No NDEF TLV found");
    pulseTagLedError();
    return false;
  }

  String mime, payload;
  if (!parseMimeRecord(buf + ndefOffset, ndefLen, mime, payload)) {
    debugPrint("[DEBUG] Failed to parse NDEF MIME record");
    pulseTagLedError();
    return false;
  }
  debugPrintf("[DEBUG] NDEF MIME=%s length=%u\n", mime.c_str(), (unsigned)payload.length());
  if (mime != "application/json") {
    debugPrint("[DEBUG] Ignored tag because MIME is not application/json");
    pulseTagLedError();
    return false;
  }

  JsonDocument probe;
  if (deserializeJson(probe, payload)) {
    debugPrint("[DEBUG] JSON decode failed");
    pulseTagLedError();
    return false;
  }
  if (!probe["protocol"].is<const char*>()) {
    debugPrint("[DEBUG] JSON has no protocol field");
    pulseTagLedError();
    return false;
  }
  if (String(probe["protocol"].as<const char*>()) != "openspool") {
    debugPrintf("[DEBUG] Ignored tag because protocol=%s\n", probe["protocol"].as<const char*>());
    pulseTagLedError();
    return false;
  }

  outJson = payload;
  return true;
}

// ============================== Setup / Loop ==============================
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(400);
  Serial.printf("\n[%s] boot %s (%s)\n", FW_NAME, FW_VERSION, FW_ITERATION);
  debugPrint("[DEBUG] Serial monitor ready");
  debugPrintf("[DEBUG] PN532 HSU wiring RX=GPIO%d TX=GPIO%d baud=%lu\n",
              PN532_RX_PIN,
              PN532_TX_PIN,
              PN532_BAUD);
  debugPrintf("[DEBUG] Tag activity LED pin=%d active=%s\n",
              TAG_LED_PIN,
              TAG_LED_ACTIVE_HIGH ? "HIGH" : "LOW");

  pinMode(TAG_LED_PIN, OUTPUT);
  setTagLed(false);

  loadSettings();
  gPrinterState.endpoint = printerChannelQueryUrl();

  bool staOk = false;
  if (strlen(gSettings.wifiSsid) > 0) {
    staOk = connectSta();
  }
  if (!staOk) {
    startPortal();
  }

  PN532Serial.begin(PN532_BAUD, SERIAL_8N1, PN532_RX_PIN, PN532_TX_PIN);
  pn532WakeupHsu();

  uint32_t version = 0;
  if (!pn532GetFirmwareVersionRaw(version)) {
    nfcReady = false;
    Serial.println("[PN532] not found");
    debugPrint("[DEBUG] PN532 firmware read failed");
  } else if (!pn532SamConfigRaw()) {
    nfcReady = false;
    Serial.println("[PN532] SAM config failed");
    debugPrint("[DEBUG] PN532 SAMConfig failed");
  } else {
    nfcReady = true;
    Serial.println("[PN532] ready");
    debugPrintf("[DEBUG] PN532 firmware IC=0x%02lX ver=%lu rev=%lu support=0x%02lX\n",
                (version >> 24) & 0xFF,
                (version >> 16) & 0xFF,
                (version >> 8) & 0xFF,
                version & 0xFF);
  }

  lastPollMs = millis();
  lastTagSeenMs = 0;
  lastPrinterQueryMs = 0;
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

  if (gTagState.active && gTagState.lastSeenMs != 0 && (now - gTagState.lastSeenMs) > TAG_ACTIVE_WINDOW_MS) {
    gTagState.active = false;
    bumpStateRevision();
  }

  if (!portalMode && (now - lastPrinterQueryMs) >= PRINTER_QUERY_MS) {
    lastPrinterQueryMs = now;
    fetchPrinterChannelState();
  }

  if (!nfcReady) {
    delay(200);
    return;
  }

  if (now - lastPollMs < TAG_POLL_MS) return;
  lastPollMs = now;

  uint8_t uid[10] = {0};
  uint8_t uidLen = 0;
  String openspoolJson;

  bool hasValidTag = readOpenSpoolFromCurrentTag(uid, uidLen, openspoolJson);
  if (hasValidTag) {
    handleValidOpenSpoolPayload(openspoolJson, uid, uidLen);
    return;
  }
}
