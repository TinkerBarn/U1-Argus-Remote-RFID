/*
  U1 Argus Remote RFID - V0.1
  ---------------------------------
  Target: ESP32-C3 + PN532 (HSU/UART)

  Features (V0.1):
  - AP provisioning + Web UI (when no Wi-Fi config or STA connect fails)
  - Stores config in Preferences (NVS)
  - mDNS hostname in STA mode
  - Reads NFC tags with PN532
  - Accepts only OpenSpool JSON payload (NDEF MIME: application/json, protocol=openspool)
  - Maps OpenSpool fields to Snapmaker U1 filament_detect/set API
  - Sends update webhook only when payload changed
  - Sends clear webhook when valid tag disappears

  Required libraries:
  - Adafruit PN532
  - ArduinoJson

  NOTE:
  - This version compares against last SENT payload, not queried printer state.
  - A future version can add an active /query check against the printer.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_PN532.h>

// ============================== Version ==============================
static const char* FW_NAME = "U1 Argus Remote RFID";
static const char* FW_VERSION = "V0.1";

// ============================== Pins ==============================
// Adjust for your ESP32-C3 board wiring
static const int PN532_RX_PIN = 6;   // ESP32-C3 RX from PN532 TX
static const int PN532_TX_PIN = 7;   // ESP32-C3 TX to PN532 RX
static const uint32_t PN532_BAUD = 115200;

// ============================== Timing ==============================
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
static const uint32_t TAG_POLL_MS = 250;
static const uint32_t TAG_LOST_CLEAR_MS = 3000;

// ============================== Network defaults ==============================
static const char* AP_SSID = "U1-Argus-Setup";
static const char* AP_PASS = ""; // open AP as requested
static const uint16_t WEB_PORT = 80;

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
};

Settings gSettings = {
  "", "", "u1-argus-rfid", "192.168.1.10", 8080, 0
};

// ============================== Web ==============================
WebServer web(WEB_PORT);
bool portalMode = false;

// ============================== PN532 ==============================
HardwareSerial PN532Serial(1);
Adafruit_PN532 nfc(PN532Serial);
bool nfcReady = false;

// ============================== Runtime state ==============================
uint32_t lastPollMs = 0;
uint32_t lastTagSeenMs = 0;
bool clearSentAfterLost = false;
String lastSentFingerprint = "";

// ------------------------------ Helpers ------------------------------
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

// ------------------------------ Settings ------------------------------
static void loadSettings() {
  prefs.begin(PREF_NS, true);
  String ssid = prefs.getString("wifi_ssid", "");
  String pass = prefs.getString("wifi_pass", "");
  String host = prefs.getString("hostname", "u1-argus-rfid");
  String ip = prefs.getString("printer_ip", "192.168.1.10");
  uint16_t port = prefs.getUShort("printer_port", 8080);
  uint8_t channel = prefs.getUChar("channel", 0);
  prefs.end();

  safeCopy(gSettings.wifiSsid, sizeof(gSettings.wifiSsid), ssid);
  safeCopy(gSettings.wifiPass, sizeof(gSettings.wifiPass), pass);
  safeCopy(gSettings.hostname, sizeof(gSettings.hostname), host);
  safeCopy(gSettings.printerIp, sizeof(gSettings.printerIp), ip);
  gSettings.printerPort = port;
  gSettings.channel = (channel > 3) ? 0 : channel;
}

static void saveSettings() {
  prefs.begin(PREF_NS, false);
  prefs.putString("wifi_ssid", gSettings.wifiSsid);
  prefs.putString("wifi_pass", gSettings.wifiPass);
  prefs.putString("hostname", gSettings.hostname);
  prefs.putString("printer_ip", gSettings.printerIp);
  prefs.putUShort("printer_port", gSettings.printerPort);
  prefs.putUChar("channel", gSettings.channel);
  prefs.end();
}

// ------------------------------ Web portal ------------------------------
static String configPageHtml(const String& msg = "") {
  String body;
  body.reserve(3000);
  body += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  body += F("<title>U1 Argus RFID Setup</title><style>body{font-family:Arial,sans-serif;max-width:760px;margin:20px auto;padding:0 12px}label{display:block;margin-top:12px;font-weight:600}input,select{width:100%;padding:10px;margin-top:4px}button{margin-top:16px;padding:10px 14px}small{color:#444}code{background:#eee;padding:2px 5px;border-radius:4px}</style></head><body>");
  body += F("<h2>U1 Argus Remote RFID - Setup</h2>");
  body += F("<p><small>Firmware: ");
  body += FW_VERSION;
  body += F("</small></p>");
  if (msg.length()) {
    body += F("<p><b>");
    body += htmlEscape(msg);
    body += F("</b></p>");
  }
  body += F("<form method='POST' action='/save'>");
  body += F("<label>Wi-Fi SSID</label><input name='ssid' maxlength='32' required value='"); body += htmlEscape(gSettings.wifiSsid); body += F("'>");
  body += F("<label>Wi-Fi Password</label><input name='pass' maxlength='64' value='"); body += htmlEscape(gSettings.wifiPass); body += F("'>");
  body += F("<label>Hostname (mDNS, ohne .local)</label><input name='hostname' maxlength='32' required value='"); body += htmlEscape(gSettings.hostname); body += F("'>");
  body += F("<label>Snapmaker U1 IP</label><input name='printer_ip' maxlength='39' required value='"); body += htmlEscape(gSettings.printerIp); body += F("'>");
  body += F("<label>Snapmaker U1 Port</label><input name='printer_port' type='number' min='1' max='65535' required value='"); body += String(gSettings.printerPort); body += F("'>");
  body += F("<label>Channel (Tool Head)</label><select name='channel'>");
  for (int i = 0; i < 4; i++) {
    body += "<option value='" + String(i) + "'" + String(gSettings.channel == i ? " selected" : "") + ">" + String(i) + "</option>";
  }
  body += F("</select>");
  body += F("<button type='submit'>Save & Reboot</button></form>");
  body += F("<p><small>API target: <code>/printer/filament_detect/set</code></small></p>");
  body += F("</body></html>");
  return body;
}

static void setupPortalRoutes() {
  web.on("/", HTTP_GET, []() {
    web.send(200, "text/html; charset=utf-8", configPageHtml());
  });

  web.on("/save", HTTP_POST, []() {
    String ssid = web.arg("ssid"); ssid.trim();
    String pass = web.arg("pass");
    String hostname = web.arg("hostname"); hostname.trim();
    String printerIp = web.arg("printer_ip"); printerIp.trim();
    String portStr = web.arg("printer_port"); portStr.trim();
    String channelStr = web.arg("channel"); channelStr.trim();

    uint32_t portVal = portStr.toInt();
    int ch = channelStr.toInt();

    if (ssid.length() == 0) {
      web.send(400, "text/html; charset=utf-8", configPageHtml("SSID darf nicht leer sein."));
      return;
    }
    if (hostname.length() == 0) {
      web.send(400, "text/html; charset=utf-8", configPageHtml("Hostname darf nicht leer sein."));
      return;
    }
    if (ch < 0 || ch > 3) {
      web.send(400, "text/html; charset=utf-8", configPageHtml("Channel muss 0..3 sein."));
      return;
    }
    if (!parseIpPort(printerIp.c_str(), (uint16_t)portVal)) {
      web.send(400, "text/html; charset=utf-8", configPageHtml("Printer IP/Port ist ungueltig."));
      return;
    }

    safeCopy(gSettings.wifiSsid, sizeof(gSettings.wifiSsid), ssid);
    safeCopy(gSettings.wifiPass, sizeof(gSettings.wifiPass), pass);
    safeCopy(gSettings.hostname, sizeof(gSettings.hostname), hostname);
    safeCopy(gSettings.printerIp, sizeof(gSettings.printerIp), printerIp);
    gSettings.printerPort = (uint16_t)portVal;
    gSettings.channel = (uint8_t)ch;

    saveSettings();

    String ok = F("<html><body><h3>Saved. Rebooting...</h3></body></html>");
    web.send(200, "text/html; charset=utf-8", ok);
    delay(500);
    ESP.restart();
  });

  web.onNotFound([]() {
    web.send(404, "text/plain", "Not found");
  });
}

static void startPortal() {
  portalMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  setupPortalRoutes();
  web.begin();

  IPAddress apIp = WiFi.softAPIP();
  Serial.printf("[PORTAL] AP started: SSID=%s, IP=%s\n", AP_SSID, apIp.toString().c_str());
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

  if (strlen(gSettings.hostname) > 0) {
    if (MDNS.begin(gSettings.hostname)) {
      Serial.printf("[MDNS] http://%s.local/\n", gSettings.hostname);
    } else {
      Serial.println("[MDNS] start failed");
    }
  }

  // Start status endpoint in STA mode too (read-only)
  setupPortalRoutes();
  web.begin();
  portalMode = false;

  return true;
}

// ------------------------------ NFC/OpenSpool parsing ------------------------------
static bool readPageRetry(uint8_t page, uint8_t* out) {
  for (uint8_t i = 0; i < 4; i++) {
    if (nfc.ntag2xx_ReadPage(page, out)) return true;
    delay(8);
  }
  return false;
}

static bool readNtagUserArea(uint8_t* out, size_t outLen) {
  const uint8_t first = 4;
  const uint8_t last = 129;
  size_t needed = (last - first + 1) * 4;
  if (outLen < needed) return false;

  size_t off = 0;
  for (uint8_t p = first; p <= last; p++) {
    if (!readPageRetry(p, out + off)) return false;
    off += 4;
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
static String filamentDetectUrl() {
  return String("http://") + gSettings.printerIp + ":" + String(gSettings.printerPort) + "/printer/filament_detect/set";
}

static bool postJson(const String& url, const String& payload, int& httpCode, String& resp) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.setTimeout(2500);
  if (!http.begin(url)) return false;

  http.addHeader("Content-Type", "application/json");
  httpCode = http.POST((uint8_t*)payload.c_str(), payload.length());
  resp = (httpCode > 0) ? http.getString() : "";
  http.end();

  return httpCode > 0;
}

static void sendClearIfNeeded() {
  if (clearSentAfterLost) return;
  if (WiFi.status() != WL_CONNECTED) return;

  JsonDocument req;
  req["channel"] = gSettings.channel;
  String payload;
  serializeJson(req, payload);

  int code = 0;
  String resp;
  bool ok = postJson(filamentDetectUrl(), payload, code, resp);
  Serial.printf("[API] CLEAR sent=%d code=%d resp=%s\n", ok ? 1 : 0, code, resp.c_str());

  if (ok && code >= 200 && code < 300) {
    clearSentAfterLost = true;
    lastSentFingerprint = "";
  }
}

static void handleValidOpenSpoolPayload(const String& openspoolJson, const uint8_t* uid, uint8_t uidLen) {
  String payload;
  String fingerprint;
  if (!buildFilamentDetectPayload(openspoolJson, uid, uidLen, payload, fingerprint)) {
    Serial.println("[NFC] OpenSpool parse failed");
    return;
  }

  lastTagSeenMs = millis();
  clearSentAfterLost = false;

  if (fingerprint == lastSentFingerprint) {
    return; // unchanged
  }

  int code = 0;
  String resp;
  bool ok = postJson(filamentDetectUrl(), payload, code, resp);

  Serial.printf("[API] SET sent=%d code=%d payload=%s\n", ok ? 1 : 0, code, payload.c_str());

  if (ok && code >= 200 && code < 300) {
    lastSentFingerprint = fingerprint;
  }
}

// ------------------------------ Tag polling ------------------------------
static bool readOpenSpoolFromCurrentTag(uint8_t* uid, uint8_t& uidLen, String& outJson) {
  uidLen = 0;
  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 50)) {
    return false;
  }

  static uint8_t buf[(129 - 4 + 1) * 4];
  if (!readNtagUserArea(buf, sizeof(buf))) return false;

  size_t ndefOffset = 0, ndefLen = 0;
  if (!findNdefTlv(buf, sizeof(buf), ndefOffset, ndefLen)) return false;

  String mime, payload;
  if (!parseMimeRecord(buf + ndefOffset, ndefLen, mime, payload)) return false;
  if (mime != "application/json") return false;

  JsonDocument probe;
  if (deserializeJson(probe, payload)) return false;
  if (!probe["protocol"].is<const char*>()) return false;
  if (String(probe["protocol"].as<const char*>()) != "openspool") return false;

  outJson = payload;
  return true;
}

// ============================== Setup / Loop ==============================
void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.printf("\n[%s] boot %s\n", FW_NAME, FW_VERSION);

  loadSettings();

  bool staOk = false;
  if (strlen(gSettings.wifiSsid) > 0) {
    staOk = connectSta();
  }
  if (!staOk) {
    startPortal();
  }

  PN532Serial.begin(PN532_BAUD, SERIAL_8N1, PN532_RX_PIN, PN532_TX_PIN);
  nfc.begin();
  uint32_t version = nfc.getFirmwareVersion();
  if (!version) {
    nfcReady = false;
    Serial.println("[PN532] not found");
  } else {
    nfc.SAMConfig();
    nfcReady = true;
    Serial.println("[PN532] ready");
  }

  lastPollMs = millis();
  lastTagSeenMs = 0;
  clearSentAfterLost = true;
}

void loop() {
  web.handleClient();

  if (!nfcReady) {
    delay(200);
    return;
  }

  uint32_t now = millis();
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

  // no valid OpenSpool tag at the moment
  if (lastTagSeenMs != 0 && (now - lastTagSeenMs) > TAG_LOST_CLEAR_MS) {
    sendClearIfNeeded();
  }
}
