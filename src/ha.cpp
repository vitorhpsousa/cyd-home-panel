#include "ha.h"
#include "config.h"
#include "settings_check.h"   // HIVE_BOOST
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char *haEntityId(int e) { return cfgEntity(e); }

static EntVal g[E_COUNT];
static SemaphoreHandle_t mtx;
static volatile uint32_t lastOkMs = 0;
static volatile bool lastOk = false;

// Service calls run in their own short-lived task each, with their own HTTP connection: HA only
// answers a Bluetooth relay command when the relay has responded (5-40 s), and that must not hold up
// other commands or the polling. While a command is in flight its entity is re-read every second.
enum CmdType { CMD_SWITCH, CMD_BOOST, CMD_BOOST_OFF, CMD_SCRIPT, CMD_SELECT, CMD_LOG };
struct Cmd { CmdType t; int ent; float temp; int minutes; bool on; char text[64]; };
#define MAX_INFLIGHT 4
static SemaphoreHandle_t slots;                  // counting semaphore: commands in flight
static volatile bool inflight[E_COUNT];          // one command per entity at a time
static volatile uint32_t watchUntil[E_COUNT];    // re-read this entity every second until then

void haSnapshot(EntVal out[E_COUNT]) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  memcpy(out, g, sizeof(g));
  xSemaphoreGive(mtx);
}
bool haOnline() { return lastOk && millis() - lastOkMs < 60000; }
bool haBusy() { for (int e = 0; e < E_COUNT; e++) if (inflight[e]) return true; return false; }

static bool startCmd(const Cmd &c);
// The service domain for a Controls tile: the part of its entity ID before the dot, if it is one the
// panel knows how to switch on and off ("" otherwise, and nothing is sent).
static const char *tileDomain(const char *id) {
  static const char *const OK[] = {"switch", "light", "input_boolean"};
  for (const char *d : OK) {
    size_t n = strlen(d);
    if (!strncmp(id, d, n) && id[n] == '.') return d;
  }
  return "";
}
bool haSet(int e, bool on) {
  if (e < S_TILE1 || e > S_TILE8 || !*cfgEntity(e)) return false;   // safety: only the Controls-page switches
  if (!*tileDomain(cfgEntity(e))) return false;
  return startCmd({CMD_SWITCH, e, 0, 0, on});
}
void haBoost(float t, int m) {
  if (!HIVE_BOOST || !*cfgEntity(E_CLIMATE)) return;
  startCmd({CMD_BOOST, E_CLIMATE, t, m, true});
}
void haBoostOff() {
  if (!HIVE_BOOST || !*cfgEntity(E_CLIMATE)) return;
  startCmd({CMD_BOOST_OFF, E_CLIMATE, 0, 0, false});
}
bool haShower(bool start) {
  const char *script = start ? cfgEntity(S_SHOWER) : cfgShowerEndScript();
  if (!*cfgEntity(S_SHOWER) || !*script) return false;
  Cmd c = {CMD_SCRIPT, S_SHOWER, 0, 0, start};
  strlcpy(c.text, script, sizeof c.text);
  return startCmd(c);
}
bool haFlow(const char *option) {
  if (!*cfgEntity(E_FLOW) || !cfgFlowOptionValid(option)) return false;   // only the listed options
  Cmd c = {CMD_SELECT, E_FLOW, 0, 0, true};
  strlcpy(c.text, option, sizeof c.text);
  return startCmd(c);
}
bool haLog(const char *message) {
  if (!*message) return false;
  Cmd c = {CMD_LOG, X_LOG, 0, 0, true};
  strlcpy(c.text, message, sizeof c.text);
  return startCmd(c);
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
  if (!*cfgEntity(e)) return true;   // not configured
  String path = String("/api/states/") + cfgEntity(e);
  http.begin(client, cfgHaHost(), cfgHaPort(), path);
  http.addHeader("Authorization", String("Bearer ") + cfgHaToken());
  int code = http.GET();
  if (code != 200) {
    http.end();
    if (code == 404) {   // entity gone: mark unavailable, but the server is fine
      xSemaphoreTake(mtx, portMAX_DELAY); g[e].avail = false; g[e].ok = true; xSemaphoreGive(mtx);
      return true;
    }
    Serial.printf("HA GET %s -> %d\n", cfgEntity(e), code);
    return false;
  }
  JsonDocument filter;
  filter["state"] = true;
  JsonObject fa = filter["attributes"].to<JsonObject>();
  fa["temperature"] = true; fa["current_temperature"] = true; fa["hvac_action"] = true;
  fa["preset_mode"] = true; fa["end"] = true; fa["last_triggered"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) { Serial.printf("HA parse %s: %s\n", cfgEntity(e), err.c_str()); return false; }

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
  v.since = parseIso(a["last_triggered"] | "");
  v.ok = true;
  xSemaphoreTake(mtx, portMAX_DELAY); g[e] = v; xSemaphoreGive(mtx);
  return true;
}

static void cmdTask(void *arg) {
  Cmd c = *(Cmd *)arg;
  delete (Cmd *)arg;
  String path, body;
  if (c.t == CMD_SWITCH) {
    // turn_on/turn_off with the state the user saw, never "toggle": a repeat can't undo the first one
    path = String("/api/services/") + tileDomain(cfgEntity(c.ent)) + (c.on ? "/turn_on" : "/turn_off");
    body = String("{\"entity_id\":\"") + cfgEntity(c.ent) + "\"}";
  } else if (c.t == CMD_BOOST) {
    char b[200];
    snprintf(b, sizeof b, "{\"entity_id\":\"%s\",\"time_period\":\"%02d:%02d:00\",\"temperature\":%.1f}",
             cfgEntity(E_CLIMATE), c.minutes / 60, c.minutes % 60, c.temp);
    path = "/api/services/hive/boost_heating_on"; body = b;
  } else if (c.t == CMD_BOOST_OFF) {
    path = "/api/services/hive/boost_heating_off";
    body = String("{\"entity_id\":\"") + cfgEntity(E_CLIMATE) + "\"}";
  } else {   // Log page: built with ArduinoJson so names with quotes are escaped
    JsonDocument d;
    if (c.t == CMD_SCRIPT) { path = "/api/services/script/turn_on"; d["entity_id"] = c.text; }
    else if (c.t == CMD_SELECT) { path = "/api/services/input_select/select_option"; d["entity_id"] = cfgEntity(E_FLOW); d["option"] = c.text; }
    else { path = "/api/services/logbook/log"; d["name"] = cfgLogName(); d["message"] = c.text; }
    serializeJson(d, body);
  }
  {
    WiFiClient cl;
    HTTPClient h;
    h.begin(cl, cfgHaHost(), cfgHaPort(), path);
    h.addHeader("Authorization", String("Bearer ") + cfgHaToken());
    h.addHeader("Content-Type", "application/json");
    h.setTimeout(45000);   // HA answers when the device has: Bluetooth relays can take 5-40 s
    uint32_t t0 = millis();
    int code = h.POST(body);
    h.end();
    Serial.printf("HA POST %s %s -> %d (%lu ms)\n", path.c_str(), cfgEntity(c.ent), code, (unsigned long)(millis() - t0));
  }
  watchUntil[c.ent] = millis() + 5000;   // a few more quick reads to catch the final state
  inflight[c.ent] = false;
  xSemaphoreGive(slots);
  vTaskDelete(nullptr);
}

// Starts a command now, or drops it (offline, too many in flight, or one already running for that
// entity): a tap is never queued to run later.
static bool startCmd(const Cmd &c) {
  if (WiFi.status() != WL_CONNECTED || !haOnline() || inflight[c.ent]) return false;
  if (xSemaphoreTake(slots, 0) != pdTRUE) return false;
  inflight[c.ent] = true;
  watchUntil[c.ent] = millis() + 60000;
  Cmd *p = new Cmd(c);
  if (xTaskCreatePinnedToCore(cmdTask, "hacmd", 6144, p, 1, nullptr, 0) != pdPASS) {
    delete p; inflight[c.ent] = false; watchUntil[c.ent] = 0; xSemaphoreGive(slots);
    return false;
  }
  return true;
}

static void task(void *) {
  uint32_t lastFast = 0, lastSlow = 0;
  bool first = true;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) { lastOk = false; delay(500); continue; }
    uint32_t now = millis();
    // entities with a command in flight (or just finished): re-read every second
    static uint32_t lastWatch = 0;
    if (now - lastWatch >= 1000) {
      lastWatch = now;
      for (int e = 0; e < E_COUNT; e++)
        if (watchUntil[e] && (int32_t)(watchUntil[e] - now) > 0) fetch(e);
        else watchUntil[e] = 0;
    }
    bool doSlow = first || now - lastSlow >= 30000;
    bool doFast = doSlow || now - lastFast >= 5000;
    if (doFast) {
      int okCount = 0, tried = 0;
      for (int e = doSlow ? 0 : E_FIRST_FAST; e < E_COUNT; e++) {
        tried++;
        if (fetch(e)) okCount++;
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
  slots = xSemaphoreCreateCounting(MAX_INFLIGHT, MAX_INFLIGHT);
  http.setReuse(true);
  http.setTimeout(4000);
  xTaskCreatePinnedToCore(task, "ha", 12288, nullptr, 1, nullptr, 0);
}
