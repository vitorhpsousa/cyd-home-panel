// The three pages, the status bar, side navigation and banners. Landscape (480x320) and portrait
// (320x480) have their own layouts; both keep cards out of the side tap strips (EDGE px wide).
#include "ui.h"
#include <WiFi.h>
#include "settings_check.h"
#include "config.h"

int page = HOME;
const char *const PAGE_NAMES[NPAGES] = {"Home", "Energy", "Controls"};
Tile TILES[NTILES];
uint32_t pendingUntil[NTILES] = {0};

static char toast[64] = "";
static uint32_t toastUntil = 0;
void showToast(const char *s) { strlcpy(toast, s, sizeof toast); toastUntil = millis() + 3500; }

// ---- helpers ----
bool isOn(int e) { return ev[e].ok && !strcmp(ev[e].s, "on"); }
float num(int e) { return (e >= 0 && ev[e].ok && ev[e].avail) ? ev[e].v : NAN; }
uint16_t rateCol(float r) { return isnan(r) ? DIM : r < RATE_CHEAP ? GREEN : r >= RATE_PEAK ? RED : AMBER; }
const char *rateName(float r) { return isnan(r) ? "--" : r < RATE_CHEAP ? "Cheap" : r >= RATE_PEAK ? "Peak" : "Standard"; }
void fmtPower(char *b, size_t n, float w) {
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
static void card(const Rect &r, uint16_t c = CARD) { cv->fillRoundRect(r.x, r.y, r.w, r.h, 10, c); }
static void prettyWeather(const char *s, char *out, size_t n) {
  if (!strcmp(s, "partlycloudy")) { strlcpy(out, "Partly cloudy", n); return; }
  if (!strcmp(s, "clear-night")) { strlcpy(out, "Clear", n); return; }
  strlcpy(out, s, n);
  for (char *p = out; *p; p++) if (*p == '-') *p = ' ';
  if (out[0]) out[0] = toupper(out[0]);
}
void fitText(char *s, size_t n, const AAFont &f, int maxW) {
  if (aaMeasure(f, s) <= maxW) return;
  size_t len = strlen(s);
  while (len > 1) {
    do len--; while (len > 1 && (s[len] & 0xC0) == 0x80);   // step back one UTF-8 character
    while (len > 1 && s[len - 1] == ' ') len--;
    if (len + 4 > n) continue;
    strcpy(s + len, "\xE2\x80\xA6");
    if (aaMeasure(f, s) <= maxW) return;
  }
}
static void fmtTemp(char *b, size_t n, float t) { if (isnan(t)) strlcpy(b, "--", n); else snprintf(b, n, "%.1f\xC2\xB0", t); }
static float insideTemp() {
  float t = num(E_TIN);
  if (isnan(t) && ev[E_CLIMATE].ok) t = ev[E_CLIMATE].curTemp;
  return t;
}

// ---- layout rects shared with the touch code ----
Rect titleRect() { return {0, 0, 150, STATUS_H + 10}; }

static Rect homeCard(int i) {   // 0 inside, 1 outside, 2 heating, 3 today (portrait only)
  const int L = contentL(), W = contentW();
  if (!boardPortrait()) {
    const int w = (W - 16) / 3, y = 170;
    return {L + i * (w + 8), y, w, contentBottom() - 6 - y};
  }
  static const int Y[4] = {142, 210, 278, 378};
  const int h = i == 2 ? 92 : i == 3 ? contentBottom() - 4 - Y[3] : 62;
  return {L, Y[i], W, h};
}
Rect heatingRect() { return homeCard(2); }

// Controls: one compact row per switch that has a name, packed without gaps. Portrait = one column
// of up to 8 rows; landscape = 2 columns x 4 rows (filled top to bottom, then the right column).
static int tileCount() { int n = 0; for (int i = 0; i < NTILES; i++) if (*TILES[i].name) n++; return n; }
Rect tileRect(int i) {
  if (i < 0 || i >= NTILES || !*TILES[i].name) return {0, 0, 0, 0};
  int slot = 0;
  for (int j = 0; j < i; j++) if (*TILES[j].name) slot++;
  const int n = tileCount(), top = STATUS_H + 4, bottom = contentBottom() - CONTROLS_FOOT_H, gap = 4, colGap = 10;
  const int cols = boardPortrait() ? 1 : 2, perCol = boardPortrait() ? NTILES : 4;
  const int rows = min(n, perCol);
  const int h = min(56, (bottom - top - gap * (rows - 1)) / max(rows, 1));
  const int w = (contentW() - colGap * (cols - 1)) / cols;
  return {contentL() + (slot / perCol) * (w + colGap), top + (slot % perCol) * (h + gap), w, h};
}

// ---- chrome ----
void drawStatus(const struct tm &lt, bool timeOk) {
  aaDraw(cv, 12, 4, PAGE_NAMES[page], aaB17, TXT);
  if (page != HOME && timeOk) { char b[8]; strftime(b, sizeof b, "%H:%M", &lt); aaDrawCentered(cv, UI_W / 2, 4, b, aaB17, TXT); }
  float r = num(E_RATE);
  char b[32];
  int x = UI_W - 12;
  if (!haOnline()) { x -= aaDrawRight(cv, x, 4, "HA offline", aaB17, RED); }
  else if (!isnan(r)) {
    snprintf(b, sizeof b, "%.1fp", r * 100);
    x -= aaDrawRight(cv, x, 4, b, aaB17, rateCol(r)) + 6;
    if (!boardPortrait()) aaDrawRight(cv, x, 4, rateName(r), aaR17, rateCol(r));   // no room next to the clock in portrait
  }
  cv->fillRect(0, 27, UI_W, 2, haOnline() ? rateCol(r) : RED);
}

void drawNav() {
  const int dy = UI_H - DOTS_H / 2 - 1;
  for (int i = 0; i < NPAGES; i++) {
    int cx = UI_W / 2 + (i - (NPAGES - 1) / 2) * 18;   // NPAGES is odd
    if (i == page) cv->fillCircle(cx, dy, 4, BLUE); else cv->fillCircle(cx, dy, 3, FAINT);
  }
}

// ---- Home ----
static void heatingState(const char *&st, bool &hot, bool &boost) {
  const EntVal &c = ev[E_CLIMATE];
  boost = !strcmp(c.preset, "boost");
  bool heating = !strcmp(c.action, "heating");
  hot = heating || boost;
  st = !c.ok ? "--" : boost ? "Boost" : heating ? "On" : !strcmp(c.s, "off") ? "Off" : "Idle";
}

static const AAFont *const TEMP_FONTS[] = {&aaB44, &aaB36, &aaB30};

static void drawHomeLandscape(const struct tm &lt, bool timeOk) {
  char b[48];
  const int cx = UI_W / 2;
  if (timeOk) strftime(b, sizeof b, "%H:%M", &lt); else strcpy(b, "--:--");
  aaDrawCentered(cv, cx, 26, b, aaB100, TXT);
  if (timeOk) { strftime(b, sizeof b, "%A %e %B", &lt); aaDrawCentered(cv, cx, 138, b, aaR20, DIM); }
  else aaDrawCentered(cv, cx, 138, WiFi.status() == WL_CONNECTED ? "Getting time..." : "Connecting to Wi-Fi...", aaR20, DIM);

  // inside
  Rect r = homeCard(0);
  card(r);
  aaDraw(cv, r.x + 12, r.y + 8, "Inside", aaR17, DIM);
  fmtTemp(b, sizeof b, insideTemp());
  aaDraw(cv, r.x + 10, r.y + 38, b, aaFit(b, r.w - 12, TEMP_FONTS, 3), TXT);
  // outside
  r = homeCard(1);
  card(r);
  aaDraw(cv, r.x + 12, r.y + 8, "Outside", aaR17, DIM);
  fmtTemp(b, sizeof b, num(E_TOUT));
  aaDraw(cv, r.x + 10, r.y + 38, b, aaFit(b, r.w - 12, TEMP_FONTS, 3), BLUE);
  if (ev[E_WEATHER].ok) {
    prettyWeather(ev[E_WEATHER].s, b, sizeof b);
    fitText(b, sizeof b, aaR14, r.w - 24);
    aaDraw(cv, r.x + 12, r.y + r.h - 26, b, aaR14, DIM);
  }
  // heating
  const char *st; bool hot, boost;
  heatingState(st, hot, boost);
  r = homeCard(2);
  card(r, !strcmp(ev[E_CLIMATE].action, "heating") ? rgb(70, 35, 12) : CARD);
  aaDraw(cv, r.x + 12, r.y + 8, "Heating", aaR17, DIM);
  aaDraw(cv, r.x + 10, r.y + 36, st, aaB30, hot ? ORANGE : TXT);
  const EntVal &c = ev[E_CLIMATE];
  if (c.ok && !isnan(c.temp)) { snprintf(b, sizeof b, "Set %.1f\xC2\xB0", c.temp); aaDraw(cv, r.x + 12, r.y + r.h - 46, b, aaR17, DIM); }
  if (HIVE_BOOST) aaDraw(cv, r.x + 12, r.y + r.h - 24, boost ? "hold to stop boost" : "hold for boost", aaR14, DIM);
}

static const AAFont *const DATE_FONTS[] = {&aaR20, &aaR17};

static void drawHomePortrait(const struct tm &lt, bool timeOk) {
  char b[48], b2[24];
  const int cx = UI_W / 2;
  if (timeOk) strftime(b, sizeof b, "%H:%M", &lt); else strcpy(b, "--:--");
  aaDrawCentered(cv, cx, 24, b, aaB80, TXT);
  if (timeOk) { strftime(b, sizeof b, "%A %e %B", &lt); aaDrawCentered(cv, cx, 108, b, aaFit(b, UI_W - 20, DATE_FONTS, 2), DIM); }
  else aaDrawCentered(cv, cx, 108, WiFi.status() == WL_CONNECTED ? "Getting time..." : "Connecting to Wi-Fi...", aaR17, DIM);

  // inside / outside: full-width rows, label left, big value right
  Rect r = homeCard(0);
  card(r);
  aaDraw(cv, r.x + 12, r.y + 8, "Inside", aaR17, DIM);
  fmtTemp(b, sizeof b, insideTemp());
  aaDrawRight(cv, r.x + r.w - 10, r.y + 5, b, aaFit(b, r.w - 100, TEMP_FONTS, 3), TXT);

  r = homeCard(1);
  card(r);
  aaDraw(cv, r.x + 12, r.y + 8, "Outside", aaR17, DIM);
  fmtTemp(b, sizeof b, num(E_TOUT));
  const AAFont &tf = aaFit(b, r.w - 100, TEMP_FONTS, 3);
  int vw = aaDrawRight(cv, r.x + r.w - 10, r.y + 5, b, tf, BLUE);
  if (ev[E_WEATHER].ok) {
    char w[32];
    prettyWeather(ev[E_WEATHER].s, w, sizeof w);
    fitText(w, sizeof w, aaR14, r.w - 30 - vw);
    aaDraw(cv, r.x + 12, r.y + 36, w, aaR14, DIM);
  }

  // heating
  const char *st; bool hot, boost;
  heatingState(st, hot, boost);
  const EntVal &c = ev[E_CLIMATE];
  r = homeCard(2);
  card(r, !strcmp(c.action, "heating") ? rgb(70, 35, 12) : CARD);
  aaDraw(cv, r.x + 12, r.y + 8, "Heating", aaR17, DIM);
  aaDraw(cv, r.x + 10, r.y + 32, st, aaB30, hot ? ORANGE : TXT);
  if (c.ok && !isnan(c.temp)) { snprintf(b, sizeof b, "Set %.1f\xC2\xB0", c.temp); aaDrawRight(cv, r.x + r.w - 12, r.y + 8, b, aaR17, DIM); }
  if (HIVE_BOOST) aaDrawRight(cv, r.x + r.w - 12, r.y + r.h - 24, boost ? "hold to stop boost" : "hold for boost", aaR14, DIM);

  // today: cost, kWh and what the house is drawing now
  r = homeCard(3);
  card(r);
  aaDraw(cv, r.x + 12, r.y + 8, "Today", aaR17, DIM);
  float cost = num(E_COST), kwh = num(E_KWH), dem = num(E_DEMAND), rate = num(E_RATE);
  if (isnan(cost)) strcpy(b, "--"); else snprintf(b, sizeof b, "\xC2\xA3%.2f", cost);
  aaDraw(cv, r.x + 10, r.y + 32, b, aaB30, TXT);
  if (!isnan(kwh)) { snprintf(b, sizeof b, "%.1f kWh", kwh); aaDrawRight(cv, r.x + r.w - 12, r.y + 8, b, aaR17, DIM); }
  fmtPower(b2, sizeof b2, dem);
  snprintf(b, sizeof b, "now %s", b2);
  aaDrawRight(cv, r.x + r.w - 12, r.y + 40, b, aaB17, rateCol(rate));
}

void drawHome(const struct tm &lt, bool timeOk) {
  if (boardPortrait()) drawHomePortrait(lt, timeOk); else drawHomeLandscape(lt, timeOk);
}

// ---- Energy ----
// Rate card: "● Standard 24.5p  → Peak 35.0p at 16:00 (1h 05m)" on one line, or on two lines when narrow.
// Returns the bottom of the card.
static int drawRateCard(int x, int y, int w, time_t now) {
  char s1[24], s2[24], s3[40];
  float r = num(E_RATE), nr = num(E_NEXT);
  bool next = !isnan(r) && ev[E_RATE].end && !isnan(nr);
  if (isnan(r)) strcpy(s1, "--"); else snprintf(s1, sizeof s1, "%s %.1fp", rateName(r), r * 100);
  if (next) {
    char hm[8], d[16];
    fmtHm(hm, sizeof hm, ev[E_RATE].end);
    fmtDur(d, sizeof d, (long)(ev[E_RATE].end - now));
    snprintf(s2, sizeof s2, "%s %.1fp", rateName(nr), nr * 100);
    snprintf(s3, sizeof s3, " at %s (%s)", hm, d);
  }
  const char *ARROW = "  \xE2\x86\x92 ";
  const int tx = x + 30, room = x + w - 10 - tx;
  int w1 = aaMeasure(aaB17, s1);
  bool twoLines = next && w1 + aaMeasure(aaR17, ARROW) + aaMeasure(aaB17, s2) + aaMeasure(aaR14, s3) > room;
  const int h = twoLines ? 52 : 34;
  card({x, y, w, h});
  cv->fillCircle(x + 16, y + 17, 6, rateCol(r));
  aaDraw(cv, tx, y + 7, s1, aaB17, rateCol(r));
  if (!next) return y + h;
  int px, py;
  if (twoLines) { px = tx; py = y + 28; px += aaDraw(cv, px, py, "\xE2\x86\x92 ", aaR17, DIM); }
  else { px = tx + w1; py = y + 7; px += aaDraw(cv, px, py, ARROW, aaR17, DIM); }
  px += aaDraw(cv, px, py, s2, aaB17, rateCol(nr));
  static const AAFont *const fits[] = {&aaR17, &aaR14};
  const AAFont &f = aaFit(s3, x + w - 10 - px, fits, 2);
  fitText(s3, sizeof s3, f, x + w - 10 - px);
  aaDraw(cv, px, f.px == 17 ? py : py + 2, s3, f, DIM);
  return y + h;
}

static void drawPlugs(int top, int bottom, int cols, int rowsPerCol, int rowH) {
  char b[24];
  struct P { const char *n; int pw; int cost; } ps[NPLUGS];
  int N = 0;
  for (int i = 0; i < NPLUGS; i++)
    if (*cfgPlugName(i)) ps[N++] = {cfgPlugName(i), P_PLUG1 + i, *haEntityId(C_PLUG1 + i) ? C_PLUG1 + i : -1};
  for (int i = 1; i < N; i++)   // sort by watts, highest first
    for (int j = i; j > 0; j--) {
      float a = num(ps[j].pw), bb = num(ps[j - 1].pw);
      if (isnan(a)) a = -1;
      if (isnan(bb)) bb = -1;
      if (a > bb) { P t = ps[j]; ps[j] = ps[j - 1]; ps[j - 1] = t; } else break;
    }
  float mx = N ? max(1.0f, num(ps[0].pw)) : 1.0f;
  const int gap = 8, cw = (contentW() - gap * (cols - 1)) / cols;
  for (int i = 0; i < min(N, cols * rowsPerCol); i++) {
    int col = i / rowsPerCol, row = i % rowsPerCol;
    int x = contentL() + col * (cw + gap), y = top + row * rowH, w = cw;
    if (y + rowH > bottom + 2) break;
    float wv = num(ps[i].pw);
    fmtPower(b, sizeof b, wv);
    int left = x + w - 4 - aaDrawRight(cv, x + w - 4, y, b, aaB17, TXT);   // left edge of the watts
    float c = ps[i].cost >= 0 ? num(ps[i].cost) : NAN;
    if (!isnan(c)) { snprintf(b, sizeof b, "\xC2\xA3%.2f", c); left -= 8 + aaDrawRight(cv, left - 8, y + 2, b, aaR14, DIM); }
    // the name gets what is left; long names are cut short with "…"
    char nm[40]; strlcpy(nm, ps[i].n, sizeof nm);
    fitText(nm, sizeof nm, aaR17, left - 6 - (x + 4));
    aaDraw(cv, x + 4, y, nm, aaR17, TXT);
    int bw = isnan(wv) ? 0 : (int)((w - 8) * min(1.0f, wv / mx));
    const int by = y + min(rowH - 2, 21);   // just under the text
    cv->fillRect(x + 4, by, w - 8, 2, CARD);
    if (bw > 0) cv->fillRect(x + 4, by, bw, 2, BLUE);
  }
}

void drawEnergy(time_t now) {
  char b[64], b2[24], b3[24];
  const bool port = boardPortrait();
  const int L = contentL(), W = contentW();
  float dem = num(E_DEMAND), r = num(E_RATE);
  // using now + today: side by side in landscape, stacked in portrait
  Rect ru, rt;
  if (port) { ru = {L, 34, W, 88}; rt = {L, 128, W, 88}; }
  else { int w = (W - 8) / 2; ru = {L, 34, w, 96}; rt = {L + w + 8, 34, w, 96}; }
  static const AAFont *const BIG[] = {&aaB44, &aaB36};
  const int vy = port ? 20 : 26, sy = ru.h - (port ? 19 : 22);

  card(ru);
  aaDraw(cv, ru.x + 12, ru.y + 6, "Using now", aaR17, DIM);
  fmtPower(b, sizeof b, dem);
  aaDraw(cv, ru.x + 10, ru.y + vy, b, aaFit(b, ru.w - 20, BIG, 2), rateCol(r));
  float plugs = 0;
  for (int e = P_PLUG1; e <= P_PLUG10; e++) { float w = num(e); if (!isnan(w)) plugs += w; }
  if (!isnan(dem)) {
    fmtPower(b2, sizeof b2, plugs);
    fmtPower(b3, sizeof b3, max(0.0f, dem - plugs));
    snprintf(b, sizeof b, "plugs %s \xC2\xB7 other %s", b2, b3);
    if (aaMeasure(aaR14, b) <= ru.w - 24) aaDraw(cv, ru.x + 12, ru.y + sy, b, aaR14, DIM);
    else {   // narrow card: "plugs …" left, "other …" right
      snprintf(b, sizeof b, "plugs %s", b2); aaDraw(cv, ru.x + 12, ru.y + sy, b, aaR14, DIM);
      snprintf(b, sizeof b, "other %s", b3); aaDrawRight(cv, ru.x + ru.w - 12, ru.y + sy, b, aaR14, DIM);
    }
  }

  card(rt);
  aaDraw(cv, rt.x + 12, rt.y + 6, "Today", aaR17, DIM);
  float cost = num(E_COST), kwh = num(E_KWH);
  if (isnan(cost)) strcpy(b, "--"); else snprintf(b, sizeof b, "\xC2\xA3%.2f", cost);
  aaDraw(cv, rt.x + 10, rt.y + vy, b, aaFit(b, rt.w - 20, BIG, 2), TXT);
  if (!isnan(kwh)) { snprintf(b, sizeof b, "%.1f kWh", kwh); aaDraw(cv, rt.x + 12, rt.y + sy, b, aaR14, DIM); }

  int y = drawRateCard(L, rt.y + rt.h + 6, W, now) + 8;
  // plugs: top 8 by watts; 2 columns x 4 rows in landscape, one column in portrait
  if (port) drawPlugs(y, contentBottom() - 2, 1, 8, 22);
  else drawPlugs(y, contentBottom() - 2, 2, 4, 25);
}

// ---- Controls ----
static void toggleSwitch(int x, int y, bool on, bool known, bool pending) {
  const int w = 52, h = 28, r = h / 2;
  if (!known) { cv->drawRoundRect(x, y, w, h, r, FAINT); cv->drawRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, FAINT); return; }
  cv->fillRoundRect(x, y, w, h, r, on ? ONFILL : rgb(58, 68, 86));
  const int kx = on ? x + w - r : x + r;
  cv->fillCircle(kx, y + r, r - 4, pending ? DIM : on ? TXT : rgb(170, 180, 196));
}

void drawControls() {
  char b[40];
  for (int i = 0; i < NTILES; i++) {
    const Tile &t = TILES[i];
    if (!*t.name) continue;   // unused switch
    Rect r = tileRect(i);
    bool known = ev[t.sw].ok && ev[t.sw].avail;
    bool on = isOn(t.sw);
    bool pending = millis() < pendingUntil[i];
    card(r, on ? CARD_HI : CARD);
    if (pending) { cv->drawRoundRect(r.x, r.y, r.w, r.h, 10, TXT); cv->drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 9, TXT); }
    const int sx = r.x + r.w - 14 - 52;
    toggleSwitch(sx, r.y + (r.h - 28) / 2, on, known, pending);
    // name, with the watts / status line under it (or beside it when the row is short)
    const int nx = r.x + 14, room = sx - 12 - nx;
    char nm[40]; strlcpy(nm, t.name, sizeof nm);
    fitText(nm, sizeof nm, aaB20, room);
    const char *sub = nullptr;
    uint16_t subCol = DIM;
    if (pending) sub = "sending...";
    else if (!known) { sub = "Unavailable"; subCol = RED; }
    else { float wv = num(t.pw); if (!isnan(wv)) { fmtPower(b, sizeof b, wv); sub = b; } }
    if (sub && r.h >= 46) {
      const int top = r.y + (r.h - 24 - 17) / 2;
      aaDraw(cv, nx, top, nm, aaB20, TXT);
      aaDraw(cv, nx, top + 23, sub, aaR14, subCol);
    } else {   // short row: status to the left of the switch, name gets the rest
      int sw = sub ? aaDrawRight(cv, sx - 12, r.y + (r.h - 17) / 2 + 1, sub, aaR14, subCol) + 10 : 0;
      strlcpy(nm, t.name, sizeof nm);
      fitText(nm, sizeof nm, aaB20, room - sw);
      aaDraw(cv, nx, r.y + (r.h - 24) / 2, nm, aaB20, TXT);
    }
  }
  if (!tileCount()) aaDrawCentered(cv, UI_W / 2, UI_H / 2 - 12, "No switches set up", aaR20, DIM);
}

// ---- banners ----
static void drawBanner(int y, const char *s, uint16_t col) {
  static const AAFont *const F[] = {&aaB20, &aaB17, &aaR14};
  const int x = 10, w = UI_W - 20;
  char t[64]; strlcpy(t, s, sizeof t);
  const AAFont &f = aaFit(t, w - 20, F, 3);
  fitText(t, sizeof t, f, w - 20);
  cv->fillRoundRect(x, y, w, 38, 10, col);
  aaDrawCentered(cv, UI_W / 2, y + (38 - f.height) / 2, t, f, TXT);
}

void drawOverlays(bool tank) {
  int bottom = contentBottom() - 4;
  if (millis() < toastUntil) drawBanner(bottom - 38, toast, CARD_HI);
  else if (tank) drawBanner(bottom - 38, ALERT_TEXT, RED);
}
