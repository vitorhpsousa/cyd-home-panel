// Wi-Fi and the setup hotspot.
// Normal running: station only, joins the home Wi-Fi from config.
// Setup mode (hidden menu, or automatically when no Wi-Fi is set): AP+STA, so Home Assistant keeps
// working while someone fills in the setup page from their phone. The web server and the captive-portal
// DNS only run in setup mode, and only answer clients that are on the hotspot (never the home LAN).
#include "net.h"
#include "config.h"
#include "setup_page.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>

static const uint32_t IDLE_STOP_MS = 15UL * 60UL * 1000UL;   // hotspot turns itself off after 15 min unused
static const uint32_t STA_RETRY_MS = 60000;                  // re-join the home Wi-Fi every minute while it's down
static const int MAX_ENTITIES = 1500;                        // suggestion list cap

static WebServer *server = nullptr;
static DNSServer *dns = nullptr;
static bool setupOn = false, autoSetup = false;
static char apName[24];
static IPAddress apIp(192, 168, 4, 1);
static uint32_t lastUseMs = 0, staDownSince = 0, staLastTry = 0, restartAt = 0;

// ---- PSRAM allocator for the big Home Assistant entity list -----------------------------------
struct PsramAlloc : ArduinoJson::Allocator {
  void *allocate(size_t n) override {
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
  }
  void deallocate(void *p) override { free(p); }
  void *reallocate(void *p, size_t n) override {
    void *q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return q ? q : realloc(p, n);
  }
};
static PsramAlloc psram;

// ---- background job: "Test connection" + entity list --------------------------------------------
// Runs in its own task so the screen stays responsive while HA sends a few hundred KB.
// The loop task only starts a job when none is running, and only reads the results once done.
enum JobState : uint8_t { J_IDLE, J_RUNNING, J_OK, J_FAIL };
static volatile JobState jobState = J_IDLE;
static char jobHost[64], jobToken[304], jobMsg[96];
static uint16_t jobPort;
static char *entList = nullptr;      // JSON, in PSRAM, owned by the loop task once jobState != J_RUNNING
static size_t entListLen = 0;
static volatile int entCount = 0;

static bool domainWanted(const char *id, const char **dom) {
  static const char *const D[] = {"sensor.", "switch.", "climate.", "weather."};
  for (const char *d : D)
    if (!strncmp(id, d, strlen(d))) { *dom = d; return true; }
  return false;
}

static void runJob() {
  WiFiClient c;
  HTTPClient h;
  h.setConnectTimeout(4000);
  h.setTimeout(6000);
  String auth = String("Bearer ") + jobToken;
  h.begin(c, jobHost, jobPort, "/api/");
  h.addHeader("Authorization", auth);
  int code = h.GET();
  h.end();
  if (code != 200) {
    if (code == 401 || code == 403) strlcpy(jobMsg, "Home Assistant answered, but the token was not accepted.", sizeof jobMsg);
    else if (code < 0) snprintf(jobMsg, sizeof jobMsg, "Can't reach %s on port %u. Check the IP address and port.", jobHost, jobPort);
    else snprintf(jobMsg, sizeof jobMsg, "Unexpected answer (HTTP %d). Is this really Home Assistant?", code);
    memset(jobToken, 0, sizeof jobToken);
    jobState = J_FAIL;
    return;
  }

  // Connection is fine. Now the entity list for the suggestions (best effort).
  // /api/states can be hundreds of KB: stream it, keep only entity_id + friendly_name, all in PSRAM.
  // HTTP/1.0 so the reply is never chunked (getStream() hands us the raw body).
  h.useHTTP10(true);
  h.setTimeout(15000);
  h.begin(c, jobHost, jobPort, "/api/states");
  h.addHeader("Authorization", auth);
  code = h.GET();
  memset(jobToken, 0, sizeof jobToken);
  auth = "";
  int n = 0;
  if (code == 200) {
    JsonDocument filter;
    filter[0]["entity_id"] = true;
    filter[0]["attributes"]["friendly_name"] = true;
    JsonDocument doc(&psram);
    DeserializationError err = deserializeJson(doc, h.getStream(), DeserializationOption::Filter(filter));
    h.end();
    if (!err) {
      // Output: {"sensor.":[[id,name],...],"switch.":[...],...}
      JsonDocument out(&psram);
      for (JsonObject o : doc.as<JsonArray>()) {
        const char *id = o["entity_id"] | "";
        const char *dom;
        if (!domainWanted(id, &dom) || strlen(id) > 100) continue;
        JsonArray arr = out[dom].isNull() ? out[dom].to<JsonArray>() : out[dom].as<JsonArray>();
        JsonArray row = arr.add<JsonArray>();
        row.add(id);
        const char *fn = o["attributes"]["friendly_name"] | "";
        row.add(strlen(fn) > 60 ? "" : fn);
        if (++n >= MAX_ENTITIES) break;
      }
      doc.clear();
      size_t len = measureJson(out);
      char *buf = (char *)psram.allocate(len + 1);
      if (buf) {
        serializeJson(out, buf, len + 1);
        entList = buf;
        entListLen = len;
        entCount = n;
      }
    }
  } else {
    h.end();
  }
  if (n) snprintf(jobMsg, sizeof jobMsg, "Connected to Home Assistant. Found %d entities to pick from.", n);
  else strlcpy(jobMsg, "Connected to Home Assistant (couldn't load the entity list for suggestions).", sizeof jobMsg);
  jobState = J_OK;
}

static void jobTask(void *) {
  runJob();   // returns before vTaskDelete so its HTTP client and Strings are destroyed
  vTaskDelete(nullptr);
}

static void freeEntList() {
  if (entList) free(entList);
  entList = nullptr;
  entListLen = 0;
  entCount = 0;
}

// ---- web handlers ------------------------------------------------------------------------------
// Only clients on the hotspot get an answer. The server is also only running in setup mode.
static bool allowed() {
  if (!setupOn) return false;
  if (server->client().localIP() != WiFi.softAPIP()) {
    server->send(403, "text/plain", "The setup page is only available on the panel's setup hotspot.");
    return false;
  }
  lastUseMs = millis();
  return true;
}

static void sendJson(int code, const String &body) {
  server->sendHeader("Cache-Control", "no-store");
  server->send(code, "application/json", body);
}

static void staStatus(JsonObject o) {
  wl_status_t st = WiFi.status();
  o["ssid"] = cfgWifiSsid();
  o["state"] = st == WL_CONNECTED ? "connected" : (*cfgWifiSsid() ? "connecting" : "none");
  if (st == WL_CONNECTED) o["ip"] = WiFi.localIP().toString();
}

static void handleRoot() {
  if (!allowed()) return;
  server->sendHeader("Cache-Control", "no-store");
  server->send_P(200, "text/html", SETUP_HTML, sizeof(SETUP_HTML) - 1);
}

static void handleCfg() {
  if (!allowed()) return;
  JsonDocument d;
  JsonObject v = d["v"].to<JsonObject>();
  JsonObject has = d["has"].to<JsonObject>();
  char buf[128];
  for (int i = 0; i < cfgFieldCount(); i++) {
    CfgFieldInfo fi;
    cfgFieldInfo(i, &fi);
    if (fi.secret) { has[fi.key] = cfgFieldHasValue(i); continue; }   // never the value itself
    cfgFieldValue(i, buf, sizeof buf);
    v[fi.key] = buf;   // ArduinoJson copies the string
  }
  d["ap"] = apName;
  staStatus(d["sta"].to<JsonObject>());
  String s;
  serializeJson(d, s);
  sendJson(200, s);
}

static void handleStatus() {
  if (!allowed()) return;
  JsonDocument d;
  staStatus(d["sta"].to<JsonObject>());
  JobState j = jobState;
  d["test"] = j == J_RUNNING ? "running" : j == J_OK ? "ok" : j == J_FAIL ? "fail" : "idle";
  if (j == J_OK || j == J_FAIL) d["msg"] = jobMsg;
  d["n"] = j == J_OK ? (int)entCount : 0;
  String s;
  serializeJson(d, s);
  sendJson(200, s);
}

static void handleScan() {
  if (!allowed()) return;
  int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) { sendJson(200, "{\"state\":\"running\"}"); return; }
  if (n < 0 || server->hasArg("again")) {   // no results yet (or asked for a fresh scan)
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
    sendJson(200, "{\"state\":\"running\"}");
    return;
  }
  JsonDocument d;
  d["state"] = "done";
  JsonArray a = d["nets"].to<JsonArray>();
  for (int i = 0; i < n && i < 40; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;   // hidden networks
    JsonObject o = a.add<JsonObject>();
    o["s"] = s;
    o["r"] = WiFi.RSSI(i);
    o["l"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  String s;
  serializeJson(d, s);
  sendJson(200, s);
}

// Validates every submitted field first, then saves. Unknown form fields are ignored.
// Secrets: empty = keep the saved one (the "open" checkbox clears the Wi-Fi password).
static bool saveArgs(String &err, String &errKey, bool wifiOnly) {
  int n = cfgFieldCount();
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < n; i++) {
      CfgFieldInfo fi;
      cfgFieldInfo(i, &fi);
      if (wifiOnly && fi.kind != CFG_SSID && fi.kind != CFG_WPASS) continue;
      bool clear = fi.kind == CFG_WPASS && server->arg("open") == "1";
      if (!server->hasArg(fi.key) && !clear) continue;
      String v = clear ? String() : server->arg(fi.key);   // WebServer has already URL-decoded it
      if (fi.kind != CFG_SSID && fi.kind != CFG_WPASS) v.trim();   // SSIDs/passwords may have spaces
      if (fi.secret && !v.length() && !clear) continue;
      if (v.length() > 400) { errKey = fi.key; err = "is too long"; return false; }
      if (pass == 0) {
        const char *why = cfgFieldCheck(i, v.c_str());
        if (why) { errKey = fi.key; err = why; return false; }
      } else if (!cfgFieldSave(i, v.c_str())) {
        errKey = fi.key;
        err = "couldn't be saved (flash full?)";
        return false;
      }
    }
  }
  return true;
}

static void handleWifi() {   // save the Wi-Fi only and join it now (no restart), so "Test" can reach HA
  if (!allowed()) return;
  String err, key;
  if (!server->hasArg("ssid") || !server->arg("ssid").length()) { sendJson(400, "{\"ok\":false,\"err\":\"Pick a Wi-Fi network first.\"}"); return; }
  if (!saveArgs(err, key, true)) {
    JsonDocument d; d["ok"] = false; d["key"] = key; d["err"] = err;
    String s; serializeJson(d, s); sendJson(400, s);
    return;
  }
  WiFi.disconnect(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfgWifiSsid(), cfgWifiPass());
  staLastTry = millis();
  sendJson(200, "{\"ok\":true}");
}

static void handleTest() {
  if (!allowed()) return;
  if (jobState == J_RUNNING) { sendJson(200, "{\"ok\":true}"); return; }
  if (WiFi.status() != WL_CONNECTED) {
    sendJson(200, "{\"ok\":false,\"err\":\"The panel isn't on your home Wi-Fi yet. Join the Wi-Fi first.\"}");
    return;
  }
  String host = server->arg("host"), port = server->arg("port"), token = server->arg("token");
  host.trim(); port.trim(); token.trim();
  int hi = cfgFieldFind("host"), pi = cfgFieldFind("port"), ti = cfgFieldFind("token");
  const char *why = cfgFieldCheck(hi, host.c_str());
  if (!why && !host.length()) why = "is empty";
  if (why) { sendJson(200, String("{\"ok\":false,\"err\":\"Home Assistant IP address ") + why + "\"}"); return; }
  if (cfgFieldCheck(pi, port.c_str())) { sendJson(200, "{\"ok\":false,\"err\":\"Port must be 1-65535\"}"); return; }
  if (token.length() && cfgFieldCheck(ti, token.c_str())) { sendJson(200, "{\"ok\":false,\"err\":\"The token doesn't look right\"}"); return; }
  if (!token.length() && !*cfgHaToken()) { sendJson(200, "{\"ok\":false,\"err\":\"Paste the Home Assistant token first.\"}"); return; }
  strlcpy(jobHost, host.c_str(), sizeof jobHost);
  jobPort = (uint16_t)port.toInt();
  strlcpy(jobToken, token.length() ? token.c_str() : cfgHaToken(), sizeof jobToken);
  freeEntList();
  jobMsg[0] = 0;
  jobState = J_RUNNING;
  if (xTaskCreatePinnedToCore(jobTask, "setupjob", 10240, nullptr, 1, nullptr, 0) != pdPASS) {
    memset(jobToken, 0, sizeof jobToken);
    jobState = J_IDLE;
    sendJson(200, "{\"ok\":false,\"err\":\"Out of memory, try again.\"}");
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

static void handleEntities() {
  if (!allowed()) return;
  if (jobState != J_OK || !entList) { sendJson(404, "{}"); return; }
  server->sendHeader("Cache-Control", "no-store");
  server->send_P(200, "application/json", entList, entListLen);
}

static void handleSave() {
  if (!allowed()) return;
  String err, key;
  if (!saveArgs(err, key, false)) {
    JsonDocument d; d["ok"] = false; d["key"] = key; d["err"] = err;
    String s; serializeJson(d, s); sendJson(400, s);
    return;
  }
  sendJson(200, "{\"ok\":true}");
  restartAt = millis() + 1500;   // let the reply reach the phone first
}

static void handleReset() {
  if (!allowed()) return;
  if (server->arg("confirm") != "yes") { sendJson(400, "{\"ok\":false}"); return; }
  cfgResetToDefaults();
  sendJson(200, "{\"ok\":true}");
  restartAt = millis() + 1500;
}

// Captive portal: any other address (phones probing for internet) goes to the setup page.
static void handleOther() {
  if (!setupOn || server->client().localIP() != WiFi.softAPIP()) { server->send(404, "text/plain", ""); return; }
  server->sendHeader("Location", String("http://") + apIp.toString() + "/", true);
  server->send(302, "text/plain", "");
}

// ---- Wi-Fi -------------------------------------------------------------------------------------
void netBegin() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(apName, sizeof apName, "CYD-Panel-%02X%02X", mac[4], mac[5]);
  WiFi.persistent(false);   // our own NVS keeps the settings; don't let the Wi-Fi driver write flash too
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("cyd-home-panel");
  WiFi.setAutoReconnect(true);
  if (*cfgWifiSsid()) {
    WiFi.begin(cfgWifiSsid(), cfgWifiPass());
    staLastTry = millis();
  } else {
    autoSetup = true;
    netStartSetup();
  }
}

void netStartSetup() {
  if (setupOn) return;
  bool staUp = WiFi.status() == WL_CONNECTED;
  // Avoid a clash if the home network itself is 192.168.4.x.
  apIp = (staUp && WiFi.localIP()[0] == 192 && WiFi.localIP()[1] == 168 && WiFi.localIP()[2] == 4)
             ? IPAddress(192, 168, 77, 1) : IPAddress(192, 168, 4, 1);
  WiFi.mode(WIFI_AP_STA);
  // While the home Wi-Fi isn't connected, stop hunting for it: retries hop channels and make the
  // hotspot drop out. The page's "Join" button starts it again.
  if (!staUp) { WiFi.setAutoReconnect(false); WiFi.disconnect(false); }
  WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(apName, cfgSetupPass(), 1, 0, 2)) {
    Serial.println("setup: hotspot failed to start");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    return;
  }
  if (!server) server = new WebServer(80);
  if (!dns) dns = new DNSServer();
  dns->setErrorReplyCode(DNSReplyCode::NoError);
  dns->start(53, "*", apIp);
  server->on("/", HTTP_GET, handleRoot);
  server->on("/cfg", HTTP_GET, handleCfg);
  server->on("/status", HTTP_GET, handleStatus);
  server->on("/scan", HTTP_GET, handleScan);
  server->on("/wifi", HTTP_POST, handleWifi);
  server->on("/test", HTTP_POST, handleTest);
  server->on("/entities", HTTP_GET, handleEntities);
  server->on("/save", HTTP_POST, handleSave);
  server->on("/reset", HTTP_POST, handleReset);
  server->onNotFound(handleOther);
  server->begin();
  WiFi.scanNetworks(true);   // results ready by the time the phone opens the page
  setupOn = true;
  lastUseMs = millis();
  Serial.printf("setup: hotspot %s on %s\n", apName, apIp.toString().c_str());
}

void netStopSetup() {
  if (!setupOn) return;
  setupOn = false;
  if (server) { server->stop(); delete server; server = nullptr; }   // drops the registered handlers too
  if (dns) { dns->stop(); delete dns; dns = nullptr; }
  if (jobState != J_RUNNING) freeEntList();   // a running job finishes on its own; freed on next start
  WiFi.scanDelete();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (WiFi.status() != WL_CONNECTED && *cfgWifiSsid()) { WiFi.begin(cfgWifiSsid(), cfgWifiPass()); staLastTry = millis(); }
  autoSetup = false;
  Serial.println("setup: hotspot off");
}

bool netSetupActive() { return setupOn; }

int netSetupLines(char lines[3][48]) {
  if (!setupOn) return 0;
  snprintf(lines[0], 48, "Wi-Fi: %s", apName);
  snprintf(lines[1], 48, "Password: %s", cfgSetupPass());
  snprintf(lines[2], 48, "Then open http://%s", apIp.toString().c_str());
  return 3;
}

void netLoop() {
  uint32_t now = millis();
  if (restartAt && (int32_t)(now - restartAt) >= 0) { Serial.println("setup: restarting"); delay(100); ESP.restart(); }
  if (setupOn) {
    dns->processNextRequest();
    server->handleClient();
    // Auto-off after 15 min without page use and nobody on the hotspot (not when there's no Wi-Fi
    // set up at all: then the hotspot is the only way in).
    if (!autoSetup && *cfgWifiSsid() && now - lastUseMs > IDLE_STOP_MS && WiFi.softAPgetStationNum() == 0) netStopSetup();
    return;
  }
  // Home Wi-Fi watchdog: the driver's auto-reconnect gives up on some disconnect reasons.
  if (!*cfgWifiSsid()) return;
  if (WiFi.status() == WL_CONNECTED) { staDownSince = 0; return; }
  if (!staDownSince) staDownSince = now;
  if (now - staDownSince > 20000 && now - staLastTry > STA_RETRY_MS) {
    staLastTry = now;
    WiFi.disconnect(false);
    WiFi.begin(cfgWifiSsid(), cfgWifiPass());
  }
}
