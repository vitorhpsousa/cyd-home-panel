#include "ha.h"
#include "settings_check.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Every ID comes from settings.h. "" means "not used": it is never requested.
static const char *const IDS[E_COUNT] = {
  ENT_INDOOR_TEMP, ENT_OUTDOOR_TEMP, ENT_HEATING, ENT_WEATHER,
  ENT_RATE, ENT_NEXT_RATE, ENT_TODAY_KWH, ENT_TODAY_COST,
  PLUG1_COST, PLUG2_COST, PLUG3_COST, PLUG4_COST, PLUG5_COST,
  PLUG6_COST, PLUG7_COST, PLUG8_COST, PLUG9_COST, PLUG10_COST,
  ENT_DEMAND,
  PLUG1_POWER, PLUG2_POWER, PLUG3_POWER, PLUG4_POWER, PLUG5_POWER,
  PLUG6_POWER, PLUG7_POWER, PLUG8_POWER, PLUG9_POWER, PLUG10_POWER,
  TILE1_SWITCH, TILE2_SWITCH, TILE3_SWITCH, TILE4_SWITCH,
  ALERT_SWITCH,
};
const char *haEntityId(int e) { return (e >= 0 && e < E_COUNT) ? IDS[e] : ""; }

static EntVal g[E_COUNT];
static SemaphoreHandle_t mtx;
static volatile uint32_t lastOkMs = 0;
static volatile bool lastOk = false;

enum CmdType { CMD_NONE, CMD_TOGGLE, CMD_BOOST, CMD_BOOST_OFF };
struct Cmd { CmdType t; int ent; float temp; int minutes; };
static QueueHandle_t cmdQ;

void haSnapshot(EntVal out[E_COUNT]) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  memcpy(out, g, sizeof(g));
  xSemaphoreGive(mtx);
}
bool haOnline() { return lastOk && millis() - lastOkMs < 60000; }

void haToggle(int e) {
  if (e < S_TILE1 || e > S_TILE4 || !*IDS[e]) return;   // safety: only the 4 Controls-page switches can be toggled
  Cmd c = {CMD_TOGGLE, e, 0, 0}; xQueueSend(cmdQ, &c, 0);
}
void haBoost(float t, int m) {
  if (!HIVE_BOOST || !*ENT_HEATING) return;
  Cmd c = {CMD_BOOST, E_CLIMATE, t, m}; xQueueSend(cmdQ, &c, 0);
}
void haBoostOff() {
  if (!HIVE_BOOST || !*ENT_HEATING) return;
  Cmd c = {CMD_BOOST_OFF, E_CLIMATE, 0, 0}; xQueueSend(cmdQ, &c, 0);
}

// "2026-11-19T13:00:00+00:00" -> UTC epoch
static time_t parseIso(const char *s) {
  int Y, M, D, h, m, sec = 0, oh = 0, om = 0; char sign = '+';
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) < 5) return 0;
  const char *tz = s + 19;
  while (*tz && *tz != '+' && *tz != '-' && *tz != 'Z') tz++;
  if (*tz == '+' || *tz == '-') { sign = *tz; sscanf(tz + 1, "%d:%d", &oh, &om); }
  // days from civil (Howard Hinnant)
  Y -= M <= 2;
  long era = (Y >= 0 ? Y : Y - 399) / 400;
  unsigned yoe = (unsigned)(Y - era * 400);
  unsigned doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097 + (long)doe - 719468;
  long off = (oh * 3600L + om * 60L) * (sign == '-' ? -1 : 1);
  return (time_t)(days * 86400L + h * 3600L + m * 60L + sec - off);
}

static WiFiClient client;
static HTTPClient http;

static bool fetch(int e) {
  if (!*IDS[e]) return true;   // not configured
  String path = String("/api/states/") + IDS[e];
  http.begin(client, HA_HOST, HA_PORT, path);
  http.addHeader("Authorization", "Bearer " HA_TOKEN);
  int code = http.GET();
  if (code != 200) {
    http.end();
    if (code == 404) {   // entity gone: mark unavailable, but the server is fine
      xSemaphoreTake(mtx, portMAX_DELAY); g[e].avail = false; g[e].ok = true; xSemaphoreGive(mtx);
      return true;
    }
    Serial.printf("HA GET %s -> %d\n", IDS[e], code);
    return false;
  }
  JsonDocument filter;
  filter["state"] = true;
  JsonObject fa = filter["attributes"].to<JsonObject>();
  fa["temperature"] = true; fa["current_temperature"] = true; fa["hvac_action"] = true;
  fa["preset_mode"] = true; fa["end"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) { Serial.printf("HA parse %s: %s\n", IDS[e], err.c_str()); return false; }

  EntVal v = {};
  const char *st = doc["state"] | "";
  strlcpy(v.s, st, sizeof v.s);
  v.avail = strcmp(st, "unavailable") && strcmp(st, "unknown") && *st;
  char *endp; float f = strtof(st, &endp);
  v.v = (endp != st && *endp == 0) ? f : NAN;
  JsonObject a = doc["attributes"];
  v.temp = a["temperature"] | NAN;
  v.curTemp = a["current_temperature"] | NAN;
  strlcpy(v.action, a["hvac_action"] | "", sizeof v.action);
  strlcpy(v.preset, a["preset_mode"] | "", sizeof v.preset);
  v.end = parseIso(a["end"] | "");
  v.ok = true;
  v.atMs = millis();
  xSemaphoreTake(mtx, portMAX_DELAY); g[e] = v; xSemaphoreGive(mtx);
  return true;
}

static bool post(const char *path, const String &body) {
  http.begin(client, HA_HOST, HA_PORT, path);
  http.addHeader("Authorization", "Bearer " HA_TOKEN);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(20000);   // SwitchBot relays answer over Bluetooth and can take 5-15 s
  int code = http.POST(body);
  http.end();
  http.setTimeout(4000);
  Serial.printf("HA POST %s %s -> %d\n", path, body.c_str(), code);
  return code == 200;
}

static void runCmd(const Cmd &c) {
  if (c.t == CMD_TOGGLE) {
    post("/api/services/switch/toggle", String("{\"entity_id\":\"") + IDS[c.ent] + "\"}");
  } else if (c.t == CMD_BOOST) {
    char b[200];
    snprintf(b, sizeof b, "{\"entity_id\":\"%s\",\"time_period\":\"%02d:%02d:00\",\"temperature\":%.1f}",
             ENT_HEATING, c.minutes / 60, c.minutes % 60, c.temp);
    post("/api/services/hive/boost_heating_on", b);
  } else if (c.t == CMD_BOOST_OFF) {
    post("/api/services/hive/boost_heating_off", String("{\"entity_id\":\"") + ENT_HEATING + "\"}");
  }
  delay(400);   // let the integration report back
  fetch(c.ent);
}

static void task(void *) {
  uint32_t lastFast = 0, lastSlow = 0;
  bool first = true;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) { lastOk = false; delay(500); continue; }
    Cmd c;
    while (xQueueReceive(cmdQ, &c, 0) == pdTRUE) runCmd(c);
    uint32_t now = millis();
    bool doSlow = first || now - lastSlow >= 30000;
    bool doFast = doSlow || now - lastFast >= 5000;
    if (doFast) {
      int okCount = 0, tried = 0;
      for (int e = doSlow ? 0 : E_FIRST_FAST; e < E_COUNT; e++) {
        tried++;
        if (fetch(e)) okCount++;
        if (xQueueReceive(cmdQ, &c, 0) == pdTRUE) runCmd(c);   // keep taps snappy mid-sweep
      }
      lastOk = okCount > 0;
      if (lastOk) lastOkMs = millis();
      lastFast = now;
      if (doSlow) lastSlow = now;
      first = false;
      if (okCount < tried) Serial.printf("HA sweep: %d/%d ok\n", okCount, tried);
    }
    delay(50);
  }
}

void haBegin() {
  mtx = xSemaphoreCreateMutex();
  cmdQ = xQueueCreate(8, sizeof(Cmd));
  http.setReuse(true);
  http.setTimeout(4000);
  xTaskCreatePinnedToCore(task, "ha", 12288, nullptr, 1, nullptr, 0);
}
