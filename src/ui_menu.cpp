// Hidden menu: hold the page name (top left) for 0.7 s. Full-screen overlay with rotate, brightness,
// system info. Closes on "Close" or after 30 s without a touch.
#include "ui.h"
#include <WiFi.h>
#include "config.h"

static bool open_ = false, info_ = false;
static uint32_t lastTouch = 0;
static const uint32_t MENU_TIMEOUT_MS = 30000;

enum Btn { B_CLOSE, B_ROT, B_INFO, B_DN, B_UP, NBTN };

static Rect btn(int b) {
  const int W = UI_W;
  if (b == B_CLOSE) return {W - 126, 6, 116, 46};
  if (!boardPortrait()) {
    const int L = 16, R = W - 16, gw = (R - L - 10) / 2, h = 56;
    switch (b) {
      case B_ROT: return {L, 62, gw, h};
      case B_INFO: return {L + gw + 10, 62, gw, h};
      case B_DN: return {L, 130, 110, h + 8};
      default: return {R - 110, 130, 110, h + 8};   // B_UP
    }
  }
  const int L = 14, R = W - 14, h = 62;
  switch (b) {
    case B_ROT: return {L, 64, R - L, h};
    case B_DN: return {L, 140, 90, h + 16};
    case B_UP: return {R - 90, 140, 90, h + 16};
    default: return {L, 232, R - L, h};     // B_INFO
  }
}

bool menuIsOpen() { return open_; }
void menuOpen(bool info) { open_ = true; info_ = info; lastTouch = millis(); }
void menuClose() { open_ = false; info_ = false; }
void menuTouched() { lastTouch = millis(); }
void menuTick() { if (open_ && millis() - lastTouch > MENU_TIMEOUT_MS) menuClose(); }

void uiRotateNext() {
  uint8_t r = (boardRotation() + 1) & 3;
  cfgSetRotation(r);
  boardSetRotation(r);
  cv = boardCanvas();
}

// Brightness steps (backlight PWM), roughly even to the eye.
static const uint8_t BR_STEPS[] = {10, 20, 35, 55, 80, 110, 150, 200, 255};
static const int NBR = sizeof BR_STEPS;
static void stepBrightness(int dir) {
  uint8_t v = cfgBrightness();
  int i = 0;
  while (i < NBR - 1 && BR_STEPS[i] < v) i++;          // first step >= v
  if (dir > 0) { if (BR_STEPS[i] <= v && i < NBR - 1) i++; }
  else { if (i > 0) i--; }
  cfgSetBrightness(BR_STEPS[i]);
  uiApplyBacklight();
}

void menuTouch(int x, int y) {
  lastTouch = millis();
  if (btn(B_CLOSE).hit(x, y)) { if (info_) info_ = false; else menuClose(); return; }
  if (info_) return;
  if (btn(B_ROT).hit(x, y)) uiRotateNext();
  else if (btn(B_INFO).hit(x, y)) info_ = true;
  else if (btn(B_DN).hit(x, y)) stepBrightness(-1);
  else if (btn(B_UP).hit(x, y)) stepBrightness(1);
}

static void button(const Rect &r, const char *label, uint16_t fill = CARD_HI, uint16_t fg = TXT) {
  cv->fillRoundRect(r.x, r.y, r.w, r.h, 10, fill);
  static const AAFont *const F[] = {&aaB24, &aaB20, &aaB17};
  const AAFont &f = aaFit(label, r.w - 16, F, 3);
  aaDrawCentered(cv, r.x + r.w / 2, r.y + (r.h - f.height) / 2, label, f, fg);
}

static void plusMinus(const Rect &r, bool plus) {
  cv->fillRoundRect(r.x, r.y, r.w, r.h, 10, CARD_HI);
  const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  cv->fillRect(cx - 12, cy - 2, 24, 4, TXT);
  if (plus) cv->fillRect(cx - 2, cy - 12, 4, 24, TXT);
}

static void drawInfo() {
  char v[7][40];
  const char *k[7] = {"IP address", "Wi-Fi signal", "Home Assistant", "Free heap", "Free PSRAM", "Uptime", "Firmware"};
  if (WiFi.status() == WL_CONNECTED) strlcpy(v[0], WiFi.localIP().toString().c_str(), sizeof v[0]);
  else strlcpy(v[0], "not connected", sizeof v[0]);
  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    snprintf(v[1], sizeof v[1], "%d dBm (%s)", rssi, rssi > -60 ? "good" : rssi > -72 ? "ok" : "weak");
  } else strlcpy(v[1], "--", sizeof v[1]);
  strlcpy(v[2], haOnline() ? "online" : "offline", sizeof v[2]);
  snprintf(v[3], sizeof v[3], "%u KB", (unsigned)(ESP.getFreeHeap() / 1024));
  snprintf(v[4], sizeof v[4], "%u KB", (unsigned)(ESP.getFreePsram() / 1024));
  uint32_t s = millis() / 1000;
  if (s >= 86400) snprintf(v[5], sizeof v[5], "%ud %02uh %02um", s / 86400, s / 3600 % 24, s / 60 % 60);
  else snprintf(v[5], sizeof v[5], "%uh %02um %02us", s / 3600, s / 60 % 60, s % 60);
  snprintf(v[6], sizeof v[6], "%s %s", __DATE__, __TIME__);
  const bool port = boardPortrait();
  for (int i = 0; i < 7; i++) {
    uint16_t col = i == 2 ? (haOnline() ? GREEN : RED) : TXT;
    if (port) {   // label above value
      int y = 64 + i * 56;
      aaDraw(cv, 16, y, k[i], aaR17, DIM);
      aaDraw(cv, 16, y + 22, v[i], aaB20, col);
    } else {
      int y = 64 + i * 35;
      aaDraw(cv, 20, y, k[i], aaR20, DIM);
      aaDrawRight(cv, UI_W - 20, y, v[i], aaB20, col);
      if (i < 6) cv->drawFastHLine(20, y + 30, UI_W - 40, CARD);
    }
  }
}

void menuDraw() {
  cv->fillScreen(BG);
  aaDraw(cv, 16, 14, info_ ? "System info" : "Settings", aaB24, TXT);
  button(btn(B_CLOSE), info_ ? "Back" : "Close");
  if (info_) { drawInfo(); return; }

  static const char *const ORIENT[4] = {"portrait", "landscape", "portrait (flipped)", "landscape (flipped)"};
  char b[48];
  Rect r = btn(B_ROT);
  button(r, "\xE2\x86\xBB  Rotate");
  button(btn(B_INFO), "System info");

  Rect dn = btn(B_DN), up = btn(B_UP);
  plusMinus(dn, false);
  plusMinus(up, true);
  const int cx = (dn.x + dn.w + up.x) / 2;
  aaDrawCentered(cv, cx, dn.y + 4, "Brightness", aaR17, DIM);
  snprintf(b, sizeof b, "%d%%", (cfgBrightness() * 100 + 127) / 255);
  aaDrawCentered(cv, cx, dn.y + dn.h - 33, b, aaB24, TXT);

  // short hint below the buttons
  const int y = boardPortrait() ? btn(B_INFO).y + btn(B_INFO).h + 16 : btn(B_DN).y + btn(B_DN).h + 16;
  snprintf(b, sizeof b, "Screen: %s", ORIENT[boardRotation() & 3]);
  aaDraw(cv, 16, y, b, aaR17, DIM);
  aaDraw(cv, 16, y + 24, "Closes by itself after 30 s.", aaR17, DIM);
}
