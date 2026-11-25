// The three pages, the status bar, side navigation and banners. Landscape (480x320) and portrait
// (320x480) have their own layouts; both keep cards out of the side margins (EDGE px wide).
#include "ui.h"
#include <WiFi.h>
#include "settings_check.h"
#include "config.h"

int page = HOME;
const char *const PAGE_NAMES[NPAGES] = {"Home", "Energy", "Controls", "Log"};
bool pageEnabled(int p) { return p >= 0 && p < NPAGES && (p != LOG || logCount() > 0); }
int pageStep(int p, int dir) {
  for (int k = 0; k < NPAGES; k++) {
    p = (p + dir + NPAGES) % NPAGES;
    if (pageEnabled(p)) return p;
  }
  return HOME;
}
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
// Rows, the lock bar and the boost card all stay clear of the side margins, so a swipe can start there.
static int tileCount() { int n = 0; for (int i = 0; i < NTILES; i++) if (*TILES[i].name) n++; return n; }
static int safeL() { return EDGE + 4; }
static int safeR() { return UI_W - EDGE - 4; }
Rect tileRect(int i) {
  if (i < 0 || i >= NTILES || !*TILES[i].name) return {0, 0, 0, 0};
  int slot = 0;
  for (int j = 0; j < i; j++) if (*TILES[j].name) slot++;
  const int n = tileCount(), top = STATUS_H + 4, bottom = contentBottom() - CONTROLS_FOOT_H, gap = 4, colGap = 10;
  const int cols = boardPortrait() ? 1 : 2, perCol = boardPortrait() ? NTILES : 4;
  const int rows = min(n, perCol);
  const int h = min(56, (bottom - top - gap * (rows - 1)) / max(rows, 1));
  const int w = (safeR() - safeL() - colGap * (cols - 1)) / cols;
  return {safeL() + (slot / perCol) * (w + colGap), top + (slot % perCol) * (h + gap), w, h};
}
Rect controlsBarRect() {
  const int y = contentBottom() - 4 - 42;
  return {EDGE + 10, y, UI_W - 2 * (EDGE + 10), 42};
}
Rect boostCardRect() {
  const int w = UI_W - 2 * (EDGE + 6), h = 156;
  return {EDGE + 6, (UI_H - h) / 2 + 10, w, h};
}
Rect boostButtonRect(bool start) {
  Rect c = boostCardRect();
  const int bw = (c.w - 30) / 2, by = c.y + c.h - 60;
  bool left = start ? !guard.boostCancelLeft : guard.boostCancelLeft;
  return {left ? c.x + 10 : c.x + c.w - 10 - bw, by, bw, 50};
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
  int shown[NPAGES], n = 0;
  for (int i = 0; i < NPAGES; i++) if (pageEnabled(i)) shown[n++] = i;
  for (int k = 0; k < n; k++) {   // centred, 18 px apart
    int cx = UI_W / 2 + (2 * k - (n - 1)) * 9;
    if (shown[k] == page) cv->fillCircle(cx, dy, 4, BLUE); else cv->fillCircle(cx, dy, 3, FAINT);
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

// Every plug in use (1 W or more), the alert plug even when idle (so you can see it stopped), and "Other".
// Rows shrink (down to 19 px, small font below 22 px) until every entry fits; a single column that still
// can't fit them all becomes two. Only if even that is not enough are the smallest users left off the end.
static void drawPlugs(int top, int bottom, int cols, int rowH) {
  char b[24];
  // each plug, plus "Other": the rest of the house (live use minus the plugs, today's cost minus the plug costs)
  struct P { const char *n; float w; float c; } ps[NPLUGS + 1];
  int N = 0;
  float plugW = 0, plugC = 0;
  const int alertPlug = *haEntityId(S_ALERT) ? cfgAlertPlug() - 1 : -1;
  for (int i = 0; i < NPLUGS; i++) {
    if (!*cfgPlugName(i)) continue;
    float w = num(P_PLUG1 + i), c = *haEntityId(C_PLUG1 + i) ? num(C_PLUG1 + i) : NAN;
    if (!isnan(w)) plugW += w;
    if (!isnan(c)) plugC += c;
    if (i != alertPlug && (isnan(w) || w < 1)) continue;   // idle or offline plugs are left out of the list
    ps[N++] = {cfgPlugName(i), w, c};
  }
  float dem = num(E_DEMAND), cost = num(E_COST);
  if (!isnan(dem) || !isnan(cost))
    ps[N++] = {"Other", isnan(dem) ? NAN : max(0.0f, dem - plugW), isnan(cost) ? NAN : max(0.0f, cost - plugC)};
  for (int i = 1; i < N; i++)   // sort by watts, highest first
    for (int j = i; j > 0; j--) {
      float a = isnan(ps[j].w) ? -1 : ps[j].w, bb = isnan(ps[j - 1].w) ? -1 : ps[j - 1].w;
      if (a > bb) { P t = ps[j]; ps[j] = ps[j - 1]; ps[j - 1] = t; } else break;
    }
  float mx = N && !isnan(ps[0].w) ? max(1.0f, ps[0].w) : 1.0f;
  constexpr int MIN_ROW = 19;
  const int avail = bottom + 2 - top;
  if (cols == 1 && N * MIN_ROW > avail) cols = 2;
  const int need = (N + cols - 1) / cols;   // rows per column to show every entry
  if (need * rowH > avail) rowH = max(MIN_ROW, avail / max(need, 1));
  const int rowsPerCol = min(need, avail / rowH);
  const bool small = rowH < 22;
  const AAFont &nameF = small ? aaR14 : aaR17, &wattF = small ? aaR14 : aaB17;
  const int gap = 8, cw = (contentW() - gap * (cols - 1)) / cols;
  for (int i = 0; i < min(N, cols * rowsPerCol); i++) {
    int col = i / rowsPerCol, row = i % rowsPerCol;
    int x = contentL() + col * (cw + gap), y = top + row * rowH, w = cw;
    float wv = ps[i].w;
    const bool idle = isnan(wv) || wv < 1;
    fmtPower(b, sizeof b, wv);
    int left = x + w - 4 - aaDrawRight(cv, x + w - 4, y, b, wattF, idle ? DIM : TXT);   // left edge of the watts
    float c = ps[i].c;
    if (!isnan(c)) {   // the cost only if the name still gets room
      snprintf(b, sizeof b, "\xC2\xA3%.2f", c);
      int cwid = aaMeasure(aaR14, b);
      if (left - 8 - cwid - 6 - (x + 4) >= 72) left -= 8 + aaDrawRight(cv, left - 8, small ? y : y + 2, b, aaR14, DIM);
    }
    // the name gets what is left; long names are cut short with "…"
    char nm[40]; strlcpy(nm, ps[i].n, sizeof nm);
    fitText(nm, sizeof nm, nameF, left - 6 - (x + 4));
    aaDraw(cv, x + 4, y, nm, nameF, idle ? DIM : TXT);
    int bw = isnan(wv) ? 0 : (int)((w - 8) * min(1.0f, wv / mx));
    const int by = y + min(rowH - 2, small ? 17 : 21);   // just under the text
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
  // plugs in use, by watts: 2 columns in landscape, one column in portrait (two if they don't fit)
  if (port) drawPlugs(y, contentBottom() - 2, 1, 22);
  else drawPlugs(y, contentBottom() - 2, 2, 25);
}

// ---- Controls ----
// While locked everything on the rows is drawn at ~60 % towards the background.
static bool s_fade = false;
static uint16_t fade(uint16_t c) {
  if (!s_fade) return c;
  auto ch = [](uint16_t c, int sh, int mask) { return (c >> sh) & mask; };
  const int k = 154;   // 0..256: share of the original colour
  int r = (ch(c, 11, 31) * k + ch(BG, 11, 31) * (256 - k)) >> 8;
  int g = (ch(c, 5, 63) * k + ch(BG, 5, 63) * (256 - k)) >> 8;
  int b = (ch(c, 0, 31) * k + ch(BG, 0, 31) * (256 - k)) >> 8;
  return (r << 11) | (g << 5) | b;
}

static void toggleSwitch(int x, int y, bool on, bool known, bool pending) {
  const int w = 52, h = 28, r = h / 2;
  if (!known) { cv->drawRoundRect(x, y, w, h, r, fade(FAINT)); cv->drawRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, fade(FAINT)); return; }
  cv->fillRoundRect(x, y, w, h, r, fade(on ? ONFILL : rgb(58, 68, 86)));
  const int kx = on ? x + w - r : x + r;
  cv->fillCircle(kx, y + r, r - 4, fade(pending ? DIM : on ? TXT : rgb(170, 180, 196)));
}

static void padlock(int x, int y, uint16_t col, bool open) {   // 16 x 22
  const int sx = open ? x + 7 : x + 3, sy = open ? y - 4 : y;
  cv->drawRoundRect(sx, sy, 10, 16, 5, col);
  cv->drawRoundRect(sx + 1, sy + 1, 8, 14, 4, col);
  cv->fillRoundRect(x, y + 9, 16, 13, 2, col);
  cv->fillRect(x + 7, y + 13, 2, 5, open ? rgb(20, 70, 40) : CARD_HI);
}

static void drawControlsBar() {
  Rect r = controlsBarRect();
  char b[40];
  const AAFont &f = aaB20;
  if (controlsUnlocked()) {
    const uint16_t fill = rgb(20, 70, 40);
    cv->fillRoundRect(r.x, r.y, r.w, r.h, 10, fill);
    cv->drawRoundRect(r.x, r.y, r.w, r.h, 10, GREEN);
    int left = (int)((guard.unlockedUntil - millis() + 999) / 1000);
    snprintf(b, sizeof b, "Unlocked \xC2\xB7 locks in %d s", left);
    const AAFont &bf = aaMeasure(aaB17, b) <= r.w - 50 ? aaB17 : aaR14;
    int tw = aaMeasure(bf, b), x = r.x + (r.w - tw - 26) / 2;
    padlock(x, r.y + 10, GREEN, true);
    aaDraw(cv, x + 26, r.y + (r.h - bf.height) / 2, b, bf, GREEN);
  } else {
    bool flash = (int32_t)(guard.barFlashUntil - millis()) > 0;
    const uint16_t fill = flash ? AMBER : CARD_HI, fg = flash ? BG : TXT;
    cv->fillRoundRect(r.x, r.y, r.w, r.h, 10, fill);
    const char *t = "Tap to unlock";
    int tw = aaMeasure(f, t), x = r.x + (r.w - tw - 28) / 2;
    padlock(x, r.y + 10, fg, false);
    if (flash) cv->fillRect(x + 7, r.y + 23, 2, 5, fill);
    aaDraw(cv, x + 28, r.y + (r.h - f.height) / 2, t, f, fg);
  }
}

void drawControls() {
  char b[40];
  const bool confirmLive = guard.confirmRow >= 0 && (int32_t)(guard.confirmUntil - millis()) > 0;
  s_fade = !controlsUnlocked();
  // one name font for all rows: B20 if every name fits (and rows are tall enough), else B17
  const AAFont *nameFont = &aaB20;
  for (int i = 0; i < NTILES; i++) {
    if (!*TILES[i].name) continue;
    Rect r = tileRect(i);
    int sx = min(r.x + r.w - 8 - 52, UI_W - EDGE - 6 - 52);
    if (r.h < 46 || aaMeasure(aaB20, TILES[i].name) > sx - 8 - (r.x + 10)) nameFont = &aaB17;
  }
  for (int i = 0; i < NTILES; i++) {
    const Tile &t = TILES[i];
    if (!*t.name) continue;   // unused switch
    Rect r = tileRect(i);
    bool known = ev[t.sw].ok && ev[t.sw].avail;
    bool pending = millis() < pendingUntil[i];
    bool on = pending ? strcmp(pendingFrom[i], "on") != 0 : isOn(t.sw);   // like the HA app: show it switched at once
    bool confirm = confirmLive && guard.confirmRow == i;
    cv->fillRoundRect(r.x, r.y, r.w, r.h, 10, confirm ? rgb(120, 28, 28) : fade(on ? CARD_HI : CARD));
    if (pending) { cv->drawRoundRect(r.x, r.y, r.w, r.h, 10, fade(TXT)); cv->drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 9, fade(TXT)); }
    const int sx = min(r.x + r.w - 8 - 52, UI_W - EDGE - 6 - 52);   // never inside the right margin
    toggleSwitch(sx, r.y + (r.h - 28) / 2, on, known, pending);
    // name, with the watts / status line under it
    const int nx = r.x + 10, room = sx - 8 - nx;
    const char *sub = nullptr;
    uint16_t subCol = fade(DIM);
    if (confirm) { sub = "Tap again: off"; subCol = TXT; }
    else if (pending) sub = "sending...";
    else if (!known) { sub = "Unavailable"; subCol = fade(RED); }
    else { float wv = num(t.pw); if (!isnan(wv)) { fmtPower(b, sizeof b, wv); sub = b; } }
    const AAFont &nf = *nameFont;
    char nm[40]; strlcpy(nm, t.name, sizeof nm);
    fitText(nm, sizeof nm, nf, room);
    if (sub) {
      char sb[40]; strlcpy(sb, sub, sizeof sb);
      fitText(sb, sizeof sb, aaR14, room);
      const int top = r.y + (r.h - nf.height - 16) / 2;
      aaDraw(cv, nx, top, nm, nf, fade(TXT));
      aaDraw(cv, nx, top + nf.height - 1, sb, aaR14, subCol);
    } else aaDraw(cv, nx, r.y + (r.h - nf.height) / 2, nm, nf, fade(TXT));
  }
  s_fade = false;
  if (!tileCount()) aaDrawCentered(cv, UI_W / 2, UI_H / 2 - 12, "No switches set up", aaR20, DIM);
  else drawControlsBar();
}

// ---- Log ----
// Shower (runs the shower-pause script, or ends the pause), one button per LOG_BUTTONS item (each tap
// writes a time-stamped line to HA's logbook) and the flow buttons (FLOW_SELECT). Locked like Controls.
// Landscape: Shower across the top, the log buttons 3 per row, the flow buttons in the last row;
// portrait the same with 2 log buttons per row.
static LogCell LC[1 + 12 + 6];
static int LN = -1, nShower = 0, nLog = 0, nFlow = 0;
static uint32_t flashUntil[1 + 12 + 6];
static void trimCopy(char *dst, size_t n, const char *s, size_t len) {
  while (len && *s == ' ') { s++; len--; }
  while (len && s[len - 1] == ' ') len--;
  if (len >= n) len = n - 1;
  memcpy(dst, s, len); dst[len] = 0;
}
static void buildLog() {
  if (LN >= 0) return;
  LN = 0;
  if (*cfgEntity(S_SHOWER)) { LogCell &c = LC[LN++]; c = {}; c.kind = LK_SHOWER; strlcpy(c.main, "Shower", sizeof c.main); }
  nShower = LN;
  for (const char *p = cfgLogButtons(); *p && LN < 1 + 12;) {   // "1 hob;2 hobs/+ oven;..."
    const char *e = strchr(p, ';');
    size_t len = e ? (size_t)(e - p) : strlen(p);
    const char *sl = (const char *)memchr(p, '/', len);
    LogCell c = {}; c.kind = LK_LOG;
    trimCopy(c.main, sizeof c.main, p, sl ? (size_t)(sl - p) : len);
    if (sl) trimCopy(c.sub, sizeof c.sub, sl + 1, len - (sl + 1 - p));
    snprintf(c.text, sizeof c.text, *c.sub ? "%s %s" : "%s", c.main, c.sub);   // the logged line
    if (*c.main) LC[LN++] = c;
    if (!e) break;
    p = e + 1;
  }
  nLog = LN - nShower;
  if (*cfgEntity(E_FLOW))
    for (const char *p = cfgFlowOptions(); *p && LN < (int)(sizeof LC / sizeof LC[0]);) {   // "65,70,75"
      const char *e = strchr(p, ',');
      size_t len = e ? (size_t)(e - p) : strlen(p);
      LogCell c = {}; c.kind = LK_FLOW;
      trimCopy(c.text, sizeof c.text, p, len);
      snprintf(c.main, sizeof c.main, isdigit((unsigned char)c.text[0]) ? "%s\xC2\xB0" : "%s", c.text);
      strlcpy(c.sub, "flow", sizeof c.sub);
      if (*c.text) LC[LN++] = c;
      if (!e) break;
      p = e + 1;
    }
  nFlow = LN - nShower - nLog;
}
int logCount() { buildLog(); return LN; }
const LogCell &logCell(int i) { buildLog(); return LC[constrain(i, 0, max(LN - 1, 0))]; }
void logFlash(int i) { if (i >= 0 && i < LN) flashUntil[i] = millis() + 1500; }
Rect logCellRect(int i) {
  buildLog();
  if (i < 0 || i >= LN) return {0, 0, 0, 0};
  const bool port = boardPortrait();
  const int cols = port ? 2 : 3, logRows = (nLog + cols - 1) / cols;
  const int rows = (nShower ? 1 : 0) + logRows + (nFlow ? 1 : 0);
  const int top = STATUS_H + 4, bottom = contentBottom() - CONTROLS_FOOT_H, gap = 6;
  const int h = min(port ? 64 : 56, (bottom - top - gap * (rows - 1)) / max(rows, 1));
  int row, col, ncol;
  if (i < nShower) { row = 0; col = 0; ncol = 1; }
  else if (i < nShower + nLog) { int k = i - nShower; row = (nShower ? 1 : 0) + k / cols; col = k % cols; ncol = cols; }
  else { row = rows - 1; col = i - nShower - nLog; ncol = nFlow; }
  const int W = safeR() - safeL(), w = (W - gap * (ncol - 1)) / ncol;
  return {safeL() + col * (w + gap), top + row * (h + gap), w, h};
}

static void logButton(const Rect &r, const char *main, const char *sub, uint16_t fill, uint16_t fg, uint16_t subCol,
                      uint16_t outline = 0) {
  cv->fillRoundRect(r.x, r.y, r.w, r.h, 10, fade(fill));
  if (outline) { cv->drawRoundRect(r.x, r.y, r.w, r.h, 10, fade(outline)); cv->drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 9, fade(outline)); }
  static const AAFont *const F[] = {&aaB20, &aaB17};
  char m[32]; strlcpy(m, main, sizeof m);
  const AAFont &f = aaFit(m, r.w - 12, F, 2);
  fitText(m, sizeof m, f, r.w - 12);
  if (sub && *sub && r.h >= f.height + 14) {
    char s[48]; strlcpy(s, sub, sizeof s);
    fitText(s, sizeof s, aaR14, r.w - 12);
    const int top = r.y + (r.h - f.height - 15) / 2;
    aaDrawCentered(cv, r.x + r.w / 2, top, m, f, fade(fg));
    aaDrawCentered(cv, r.x + r.w / 2, top + f.height - 2, s, aaR14, fade(subCol));
  } else aaDrawCentered(cv, r.x + r.w / 2, r.y + (r.h - f.height) / 2, m, f, fade(fg));
}

void drawLog() {
  buildLog();
  s_fade = !controlsUnlocked();
  const uint32_t ms = millis();
  for (int i = 0; i < LN; i++) {
    const LogCell &c = LC[i];
    Rect r = logCellRect(i);
    if (c.kind == LK_SHOWER) {
      char sub[48];
      if (isOn(S_SHOWER)) {   // the pause script is running
        char hm[8] = "";
        if (ev[S_SHOWER].since) fmtHm(hm, sizeof hm, ev[S_SHOWER].since + cfgShowerMinutes() * 60);
        snprintf(sub, sizeof sub, *hm ? "until %s \xC2\xB7 tap to end" : "tap to end", hm);
        logButton(r, "Heating paused", sub, rgb(90, 45, 12), TXT, TXT, ORANGE);
      } else {
        bool off = ev[E_CLIMATE].ok && !strcmp(ev[E_CLIMATE].s, "off");
        if (off) strlcpy(sub, "heating is off \xC2\xB7 just logs it", sizeof sub);
        else snprintf(sub, sizeof sub, "pauses the heating %d min", cfgShowerMinutes());
        logButton(r, c.main, sub, CARD_HI, TXT, DIM);
      }
    } else if (c.kind == LK_FLOW) {
      bool sel = ev[E_FLOW].ok && !strcmp(ev[E_FLOW].s, c.text);
      logButton(r, c.main, c.sub, sel ? ONFILL : CARD, sel ? BG : TXT, sel ? BG : DIM);
    } else {
      bool fl = (int32_t)(flashUntil[i] - ms) > 0;
      logButton(r, c.main, c.sub, fl ? GREEN : CARD, fl ? BG : TXT, fl ? BG : DIM);
    }
  }
  s_fade = false;
  if (!LN) aaDrawCentered(cv, UI_W / 2, UI_H / 2 - 12, "Nothing set up", aaR20, DIM);
  else drawControlsBar();
}

// ---- banners ----
// above the page dots, or above the lock bar on the Controls page (kept visible)
static int bannerY() { return (pageLocked(page) ? controlsBarRect().y : contentBottom()) - 4 - 38; }
Rect alertBannerRect() { return {10, bannerY(), UI_W - 20, 38}; }
bool toastShowing() { return millis() < toastUntil; }

// closable = draw an "x" at the right end (the alert banner hides when tapped)
static void drawBanner(int y, const char *s, uint16_t col, bool closable = false) {
  static const AAFont *const F[] = {&aaB20, &aaB17, &aaR14};
  const int x = 10, w = UI_W - 20, xw = closable ? 36 : 0;   // room kept for the "x"
  char t[64]; strlcpy(t, s, sizeof t);
  const AAFont &f = aaFit(t, w - 20 - 2 * xw, F, 3);
  fitText(t, sizeof t, f, w - 20 - 2 * xw);
  cv->fillRoundRect(x, y, w, 38, 10, col);
  aaDrawCentered(cv, UI_W / 2, y + (38 - f.height) / 2, t, f, TXT);
  if (closable) {
    const int cx = x + w - 20, cy = y + 19, r = 6;
    for (int d = -1; d <= 1; d++) {   // 3 px thick
      cv->drawLine(cx - r + d, cy - r, cx + r + d, cy + r, TXT);
      cv->drawLine(cx - r + d, cy + r, cx + r + d, cy - r, TXT);
    }
  }
}

static void drawBoostAsk() {
  char b[40];
  Rect c = boostCardRect();
  cv->fillRoundRect(c.x - 2, c.y - 2, c.w + 4, c.h + 4, 12, BG);
  cv->fillRoundRect(c.x, c.y, c.w, c.h, 12, CARD_HI);
  cv->drawRoundRect(c.x, c.y, c.w, c.h, 12, ORANGE);
  snprintf(b, sizeof b, "Boost heating to %.0f\xC2\xB0?", guard.boostTarget);
  static const AAFont *const F[] = {&aaB24, &aaB20, &aaB17};
  aaDrawCentered(cv, c.x + c.w / 2, c.y + 16, b, aaFit(b, c.w - 20, F, 3), TXT);
  aaDrawCentered(cv, c.x + c.w / 2, c.y + 50, "for 30 minutes", aaR20, DIM);
  Rect s = boostButtonRect(true), x = boostButtonRect(false);
  cv->fillRoundRect(s.x, s.y, s.w, s.h, 10, ORANGE);
  aaDrawCentered(cv, s.x + s.w / 2, s.y + (s.h - aaB20.height) / 2, aaMeasure(aaB20, "Start boost") <= s.w - 10 ? "Start boost" : "Start", aaB20, BG);
  cv->fillRoundRect(x.x, x.y, x.w, x.h, 10, CARD);
  aaDrawCentered(cv, x.x + x.w / 2, x.y + (x.h - aaB20.height) / 2, "Cancel", aaB20, TXT);
}

void drawOverlays(bool tank) {
  if (guard.boostAsk) drawBoostAsk();
  if (toastShowing()) drawBanner(bannerY(), toast, CARD_HI);
  else if (tank) drawBanner(bannerY(), ALERT_TEXT, RED, true);
}
