// CYD home panel: 3 swipeable pages (Home, Energy, Controls) fed by Home Assistant.
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "board.h"
#include "touch.h"
#include "aatext.h"
#include "alert_audio.h"
#include "ha.h"
#include "settings_check.h"

#define LED_PIN 40
#define BL_DAY 255
#define BL_NIGHT 18
#define ALERT_MS ((uint32_t)ALERT_MINUTES * 60 * 1000)

static GFXcanvas16 *cv;
static EntVal ev[E_COUNT];

static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
static const uint16_t BG = rgb(10, 14, 22), CARD = rgb(26, 33, 46), CARD_HI = rgb(40, 50, 68),
                      TXT = 0xFFFF, DIM = rgb(140, 152, 170), GREEN = rgb(60, 205, 120), AMBER = rgb(245, 170, 40),
                      RED = rgb(235, 72, 72), BLUE = rgb(90, 165, 255), ORANGE = rgb(255, 128, 40), ONFILL = rgb(250, 190, 60);

enum Page { HOME, ENERGY, CONTROLS, NPAGES };
static int page = HOME;
static const char *PAGE_NAMES[NPAGES] = {"Home", "Energy", "Controls"};

static char toast[64] = "";
static uint32_t toastUntil = 0;
static void showToast(const char *s) { strlcpy(toast, s, sizeof toast); toastUntil = millis() + 3500; }

// ---- control tiles ----
// Settings use plug numbers 1-10 (0 = none); the code uses entity indexes.
#define PLUG_ENT(n) ((n) >= 1 && (n) <= NPLUGS ? P_PLUG1 + (n) - 1 : -1)
struct Tile { const char *name; int sw; int pw; };
static const Tile TILES[4] = {
  {TILE1_NAME, S_TILE1, PLUG_ENT(TILE1_PLUG)}, {TILE2_NAME, S_TILE1 + 1, PLUG_ENT(TILE2_PLUG)},
  {TILE3_NAME, S_TILE1 + 2, PLUG_ENT(TILE3_PLUG)}, {TILE4_NAME, S_TILE1 + 3, PLUG_ENT(TILE4_PLUG)},
};
static const char *const PLUG_NAMES[NPLUGS] = {
  PLUG1_NAME, PLUG2_NAME, PLUG3_NAME, PLUG4_NAME, PLUG5_NAME,
  PLUG6_NAME, PLUG7_NAME, PLUG8_NAME, PLUG9_NAME, PLUG10_NAME,
};
static uint32_t pendingUntil[4] = {0};
static char pendingFrom[4][8];

// ---- helpers ----
static bool isOn(int e) { return ev[e].ok && !strcmp(ev[e].s, "on"); }
static float num(int e) { return (e >= 0 && ev[e].ok && ev[e].avail) ? ev[e].v : NAN; }
static uint16_t rateCol(float r) { return isnan(r) ? DIM : r < RATE_CHEAP ? GREEN : r >= RATE_PEAK ? RED : AMBER; }
static const char *rateName(float r) { return isnan(r) ? "--" : r < RATE_CHEAP ? "Cheap" : r >= RATE_PEAK ? "Peak" : "Standard"; }
static void fmtPower(char *b, size_t n, float w) {
  if (isnan(w)) snprintf(b, n, "--");
  else if (w >= 1000) snprintf(b, n, "%.2f kW", w / 1000);
  else snprintf(b, n, "%.0f W", w);
}
static void fmtHm(char *b, size_t n, time_t t) { struct tm lt; localtime_r(&t, &lt); strftime(b, n, "%H:%M", &lt); }
static void fmtDur(char *b, size_t n, long s) {
  if (s < 0) s = 0;
  long m = (s + 59) / 60;
  if (m >= 60) snprintf(b, n, "%ldh %02ldm", m / 60, m % 60); else snprintf(b, n, "%ld min", m);
}
static void card(int x, int y, int w, int h, uint16_t c = CARD) { cv->fillRoundRect(x, y, w, h, 10, c); }
static void prettyWeather(const char *s, char *out, size_t n) {
  if (!strcmp(s, "partlycloudy")) { strlcpy(out, "Partly cloudy", n); return; }
  if (!strcmp(s, "clear-night")) { strlcpy(out, "Clear", n); return; }
  strlcpy(out, s, n);
  for (char *p = out; *p; p++) if (*p == '-') *p = ' ';
  if (out[0]) out[0] = toupper(out[0]);
}

// ---- chrome ----
static void drawStatus(const struct tm &lt, bool timeOk) {
  aaDraw(cv, 12, 4, PAGE_NAMES[page], aaB17, TXT);
  if (page != HOME && timeOk) { char b[8]; strftime(b, sizeof b, "%H:%M", &lt); aaDrawCentered(cv, 240, 4, b, aaB17, TXT); }
  float r = num(E_RATE);
  char b[32];
  int x = 468;
  if (!haOnline()) { x -= aaDrawRight(cv, x, 4, "HA offline", aaB17, RED); }
  else if (!isnan(r)) {
    snprintf(b, sizeof b, "%.1fp", r * 100);
    x -= aaDrawRight(cv, x, 4, b, aaB17, rateCol(r)) + 6;
    aaDrawRight(cv, x, 4, rateName(r), aaR17, rateCol(r));
  }
  cv->fillRect(0, 27, 480, 2, haOnline() ? rateCol(r) : RED);
}

static void drawTabs() {
  cv->fillRect(0, 274, 480, 46, CARD);
  for (int i = 0; i < NPAGES; i++) {
    int cx = 80 + i * 160;
    bool a = i == page;
    aaDrawCentered(cv, cx, 285, PAGE_NAMES[i], a ? aaB20 : aaR20, a ? TXT : DIM);
    if (a) cv->fillRoundRect(cx - 40, 312, 80, 4, 2, BLUE);
  }
}

// ---- pages ----
static void drawHome(const struct tm &lt, bool timeOk) {
  char b[48];
  if (timeOk) strftime(b, sizeof b, "%H:%M", &lt); else strcpy(b, "--:--");
  aaDrawCentered(cv, 240, 26, b, aaB100, TXT);
  if (timeOk) { strftime(b, sizeof b, "%A %e %B", &lt); aaDrawCentered(cv, 240, 138, b, aaR20, DIM); }
  else aaDrawCentered(cv, 240, 138, WiFi.status() == WL_CONNECTED ? "Getting time..." : "Connecting to Wi-Fi...", aaR20, DIM);

  const int y = 170, h = 98, w = 148;
  // inside
  card(10, y, w, h);
  aaDraw(cv, 22, y + 8, "Inside", aaR17, DIM);
  float t = num(E_TIN);
  if (isnan(t) && ev[E_CLIMATE].ok) t = ev[E_CLIMATE].curTemp;
  if (isnan(t)) strcpy(b, "--"); else snprintf(b, sizeof b, "%.1f\xC2\xB0", t);
  aaDraw(cv, 20, y + 34, b, aaB44, TXT);
  // outside
  card(166, y, w, h);
  aaDraw(cv, 178, y + 8, "Outside", aaR17, DIM);
  t = num(E_TOUT);
  if (isnan(t)) strcpy(b, "--"); else snprintf(b, sizeof b, "%.1f\xC2\xB0", t);
  aaDraw(cv, 176, y + 30, b, aaB44, BLUE);
  if (ev[E_WEATHER].ok) { prettyWeather(ev[E_WEATHER].s, b, sizeof b); aaDraw(cv, 178, y + 78, b, aaR14, DIM); }
  // heating
  const EntVal &c = ev[E_CLIMATE];
  bool boost = !strcmp(c.preset, "boost");
  bool heating = !strcmp(c.action, "heating");
  card(322, y, w, h, heating ? rgb(70, 35, 12) : CARD);
  aaDraw(cv, 334, y + 8, "Heating", aaR17, DIM);
  const char *st = !c.ok ? "--" : boost ? "Boost" : heating ? "On" : !strcmp(c.s, "off") ? "Off" : "Idle";
  aaDraw(cv, 332, y + 28, st, aaB30, heating || boost ? ORANGE : TXT);
  if (c.ok && !isnan(c.temp)) { snprintf(b, sizeof b, "Set %.1f\xC2\xB0", c.temp); aaDraw(cv, 334, y + 62, b, aaR14, DIM); }
  if (HIVE_BOOST) aaDraw(cv, 334, y + 79, boost ? "hold to stop boost" : "hold for boost", aaR14, DIM);
}

static void drawEnergy(time_t now) {
  char b[64], b2[24];
  float dem = num(E_DEMAND), r = num(E_RATE);
  // using now
  card(10, 34, 226, 92);
  aaDraw(cv, 22, 40, "Using now", aaR17, DIM);
  fmtPower(b, sizeof b, dem);
  aaDraw(cv, 20, 60, b, aaB44, rateCol(r));
  float plugs = 0;
  for (int e = P_PLUG1; e <= P_PLUG10; e++) { float w = num(e); if (!isnan(w)) plugs += w; }
  if (!isnan(dem)) {
    fmtPower(b2, sizeof b2, plugs);
    char b3[24]; fmtPower(b3, sizeof b3, max(0.0f, dem - plugs));
    snprintf(b, sizeof b, "plugs %s \xC2\xB7 other %s", b2, b3);
    aaDraw(cv, 22, 106, b, aaR14, DIM);
  }
  // today
  card(244, 34, 226, 92);
  aaDraw(cv, 256, 40, "Today", aaR17, DIM);
  float cost = num(E_COST), kwh = num(E_KWH);
  if (isnan(cost)) strcpy(b, "--"); else snprintf(b, sizeof b, "\xC2\xA3%.2f", cost);
  aaDraw(cv, 254, 60, b, aaB44, TXT);
  if (!isnan(kwh)) {
    snprintf(b, sizeof b, "%.1f kWh", kwh);
    aaDraw(cv, 256, 106, b, aaR14, DIM);
  }
  // rate line
  card(10, 132, 460, 34);
  cv->fillCircle(26, 149, 6, rateCol(r));
  if (!isnan(r)) {
    snprintf(b, sizeof b, "%s %.1fp", rateName(r), r * 100);
    int x = 40 + aaDraw(cv, 40, 139, b, aaB17, rateCol(r));
    float nr = num(E_NEXT);
    if (ev[E_RATE].end && !isnan(nr)) {
      fmtHm(b2, sizeof b2, ev[E_RATE].end);
      char d[16]; fmtDur(d, sizeof d, (long)(ev[E_RATE].end - now));
      x += aaDraw(cv, x, 139, "  \xE2\x86\x92 ", aaR17, DIM);
      snprintf(b, sizeof b, "%s %.1fp", rateName(nr), nr * 100);
      x += aaDraw(cv, x, 139, b, aaB17, rateCol(nr));
      snprintf(b, sizeof b, " at %s (%s)", b2, d);
      static const AAFont *const fits[] = {&aaR17, &aaR14};
      const AAFont &f = aaFit(b, 462 - x, fits, 2);
      aaDraw(cv, x, f.px == 17 ? 139 : 141, b, f, DIM);
    }
  }
  // plugs: top 8 by watts, 2 columns x 4 rows
  struct P { const char *n; int pw; int cost; } ps[NPLUGS];
  int N = 0;
  for (int i = 0; i < NPLUGS; i++)
    if (*PLUG_NAMES[i]) ps[N++] = {PLUG_NAMES[i], P_PLUG1 + i, *haEntityId(C_PLUG1 + i) ? C_PLUG1 + i : -1};
  for (int i = 1; i < N; i++)
    for (int j = i; j > 0; j--) {
      float a = num(ps[j].pw), bb = num(ps[j - 1].pw);
      if (isnan(a)) a = -1; if (isnan(bb)) bb = -1;
      if (a > bb) { P t = ps[j]; ps[j] = ps[j - 1]; ps[j - 1] = t; } else break;
    }
  float mx = N ? max(1.0f, num(ps[0].pw)) : 1.0f;
  for (int i = 0; i < min(N, 8); i++) {
    int col = i / 4, row = i % 4;
    int x = 10 + col * 234, y = 172 + row * 25, w = 226;
    float wv = num(ps[i].pw);
    aaDraw(cv, x + 4, y, ps[i].n, aaR17, TXT);
    fmtPower(b, sizeof b, wv);
    int rx = x + w - 4 - aaDrawRight(cv, x + w - 4, y, b, aaB17, TXT);
    float c = ps[i].cost >= 0 ? num(ps[i].cost) : NAN;
    if (!isnan(c)) { snprintf(b, sizeof b, "\xC2\xA3%.2f", c); aaDrawRight(cv, rx - 8, y + 2, b, aaR14, DIM); }
    int bw = isnan(wv) ? 0 : (int)((w - 8) * min(1.0f, wv / mx));
    cv->fillRect(x + 4, y + 21, w - 8, 2, CARD);
    if (bw > 0) cv->fillRect(x + 4, y + 21, bw, 2, BLUE);
  }
}

static void tileRect(int i, int &x, int &y, int &w, int &h) { x = 10 + (i % 2) * 235; y = 36 + (i / 2) * 120; w = 225; h = 112; }

static void drawControls() {
  char b[32];
  for (int i = 0; i < 4; i++) {
    int x, y, w, h; tileRect(i, x, y, w, h);
    const Tile &t = TILES[i];
    if (!*t.name) continue;   // unused button
    bool known = ev[t.sw].ok && ev[t.sw].avail;
    bool on = isOn(t.sw);
    bool pending = millis() < pendingUntil[i];
    card(x, y, w, h, on ? ONFILL : CARD);
    if (pending) { cv->drawRoundRect(x, y, w, h, 10, TXT); cv->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 9, TXT); }
    uint16_t fg = on ? BG : TXT, dim = on ? rgb(90, 60, 10) : DIM;
    aaDraw(cv, x + 14, y + 10, t.name, aaB24, fg);
    aaDraw(cv, x + 14, y + 46, !known ? "Unavailable" : on ? "ON" : "OFF", aaB30, known ? fg : RED);
    float wv = num(t.pw);
    if (!isnan(wv)) { fmtPower(b, sizeof b, wv); aaDrawRight(cv, x + w - 14, y + 84, b, aaR17, dim); }
    aaDraw(cv, x + 14, y + 86, pending ? "sending..." : "tap to switch", aaR14, dim);
  }
}

// ---- alerts ----
static uint32_t dehumLowSince = 0;
static bool tankAlerted = false;
static bool tankFull() {
  const int pw = PLUG_ENT(ALERT_PLUG);
  if (!isOn(S_ALERT) || isnan(num(pw))) { dehumLowSince = 0; return false; }
  if (num(pw) >= ALERT_WATTS) { dehumLowSince = 0; return false; }
  if (!dehumLowSince) dehumLowSince = millis();
  return millis() - dehumLowSince >= ALERT_MS;
}

static void drawBanner(const char *s, uint16_t col) {
  cv->fillRoundRect(40, 228, 400, 38, 10, col);
  aaDrawCentered(cv, 240, 236, s, aaB20, TXT);
}

// ---- main ----
static uint32_t wakeUntil = 0;
static bool dimmed = false;
static int lastBand = -1;

void setup() {
  Serial.begin(115200);
  delay(200);
  if (!boardInit()) { Serial.println("boardInit failed"); for (;;) delay(1000); }
  cv = boardCanvas();
  touchBegin();
  audioBegin();
  setenv("TZ", TZ_RULE, 1); tzset();
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("cyd-home-panel");
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTzTime(TZ_RULE, "pool.ntp.org", "time.google.com");
  haBegin();
  Serial.println("cyd-home-panel up");
}

void loop() {
  static uint32_t lastDraw = 0, lastWifiLog = 0;
  static bool dirty = true;
  uint32_t ms = millis();

  if (ms - lastWifiLog > 10000) {
    lastWifiLog = ms;
    Serial.printf("wifi %d ip %s rssi %d ha %d heap %u psram %u\n", WiFi.status(), WiFi.localIP().toString().c_str(),
                  WiFi.RSSI(), haOnline(), ESP.getFreeHeap(), ESP.getFreePsram());
  }

  time_t now = time(nullptr);
  struct tm lt; localtime_r(&now, &lt);
  bool timeOk = now > 1735689600;
  bool night = timeOk && (lt.tm_hour >= NIGHT_FROM || lt.tm_hour < NIGHT_TO);

  // touch
  TouchEvent te = touchPoll();
  if (te.type != TouchEvent::NONE) {
    bool wasDim = dimmed;
    wakeUntil = ms + 30000;
    dirty = true;
    if (!wasDim) {
      if (te.type == TouchEvent::SWIPE_LEFT) page = (page + 1) % NPAGES;
      else if (te.type == TouchEvent::SWIPE_RIGHT) page = (page + NPAGES - 1) % NPAGES;
      else if (te.type == TouchEvent::TAP && te.y >= 274) page = min(NPAGES - 1, te.x / 160);
      else if (te.type == TouchEvent::TAP && page == CONTROLS) {
        for (int i = 0; i < 4; i++) {
          int x, y, w, h; tileRect(i, x, y, w, h);
          if (*TILES[i].name && te.x >= x && te.x < x + w && te.y >= y && te.y < y + h && millis() >= pendingUntil[i]) {
            strlcpy(pendingFrom[i], ev[TILES[i].sw].s, sizeof pendingFrom[i]);
            pendingUntil[i] = ms + 20000;
            haToggle(TILES[i].sw);
            audioBeep(1200, 25, 25);
          }
        }
      } else if (te.type == TouchEvent::LONG && HIVE_BOOST && page == HOME && te.x >= 322 && te.y >= 170 && te.y < 270) {
        const EntVal &c = ev[E_CLIMATE];
        if (!strcmp(c.preset, "boost")) { haBoostOff(); showToast("Stopping boost"); }
        else {
          float cur = isnan(num(E_TIN)) ? c.curTemp : num(E_TIN);
          float target = isnan(cur) ? 21 : constrain(roundf(cur) + 1, 20, 25);
          haBoost(target, 30);
          char b[48]; snprintf(b, sizeof b, "Boost 30 min to %.0f\xC2\xB0" "C", target);
          showToast(b);
        }
        audioBeep(900, 60, 30);
      }
    }
  }

  // backlight
  bool wantDim = night && ms > wakeUntil;
  if (wantDim != dimmed) { dimmed = wantDim; boardSetBacklight(dimmed ? BL_NIGHT : BL_DAY); dirty = true; }

  if (!dirty && ms - lastDraw < 1000) { delay(5); return; }
  dirty = false;
  lastDraw = ms;
  haSnapshot(ev);

  // pending tiles clear when the state flips
  for (int i = 0; i < 4; i++)
    if (pendingUntil[i] && strcmp(pendingFrom[i], ev[TILES[i].sw].s)) pendingUntil[i] = 0;

  // rate LED + peak chime
  float r = num(E_RATE);
  int band = isnan(r) ? -1 : r < RATE_CHEAP ? 0 : r >= RATE_PEAK ? 2 : 1;
  uint8_t ledB = dimmed ? 0 : 10;
  if (band == 0) neopixelWrite(LED_PIN, 0, ledB, 0);
  else if (band == 1) neopixelWrite(LED_PIN, ledB, ledB / 2, 0);
  else if (band == 2) neopixelWrite(LED_PIN, ledB, 0, 0);
  else neopixelWrite(LED_PIN, 0, 0, 0);
  if (band == 2 && lastBand == 1 && !night) { audioAlert(SOOT_AMBER); showToast(PEAK_MESSAGE); }
  if (band >= 0) lastBand = band;

  bool tank = tankFull();
  if (tank && !tankAlerted) { tankAlerted = true; if (!night) audioAlert(SOOT_RED); }
  if (!tank) tankAlerted = false;

  cv->fillScreen(BG);
  drawStatus(lt, timeOk);
  if (page == HOME) drawHome(lt, timeOk);
  else if (page == ENERGY) drawEnergy(now);
  else drawControls();
  drawTabs();
  if (ms < toastUntil) drawBanner(toast, CARD_HI);
  else if (tank) drawBanner(ALERT_TEXT, RED);
  boardPresent();

  // 's' on the serial port dumps the canvas (RGB565 little-endian) for tools/screenshot.py
  int sc = Serial.available() ? Serial.read() : -1;
  if (sc >= '1' && sc <= '3') { page = sc - '1'; dirty = true; }
  if (sc == 's') {
    Serial.printf("\nSHOT %d %d\n", UI_W, UI_H);
    Serial.write((const uint8_t *)cv->getBuffer(), (size_t)UI_W * UI_H * 2);
    Serial.println("\nENDSHOT");
  }
}
