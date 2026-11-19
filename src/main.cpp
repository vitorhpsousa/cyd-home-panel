// CYD home panel: 3 pages (Home, Energy, Controls) fed by Home Assistant. Tap the left/right edge or
// swipe to change page; hold the page name for the hidden menu. Drawing lives in ui_pages.cpp / ui_menu.cpp.
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "board.h"
#include "touch.h"
#include "aatext.h"
#include "alert_audio.h"
#include "ha.h"
#include "settings_check.h"
#include "config.h"
#include "net.h"
#include "ui.h"

#define LED_PIN 40
#define BL_NIGHT 18
#define ALERT_MS ((uint32_t)ALERT_MINUTES * 60 * 1000)

GFXcanvas16 *cv;
EntVal ev[E_COUNT];
bool dimmed = false;

// Settings use plug numbers 1-10 (0 = none); the code uses entity indexes.
#define PLUG_ENT(n) ((n) >= 1 && (n) <= NPLUGS ? P_PLUG1 + (n) - 1 : -1)
static void loadTiles() {
  for (int i = 0; i < NTILES; i++) TILES[i] = {cfgTileName(i), S_TILE1 + i, PLUG_ENT(cfgTilePlug(i))};
}
static char pendingFrom[NTILES][8];

void uiApplyBacklight() { boardSetBacklight(dimmed ? BL_NIGHT : cfgBrightness()); }

// ---- alerts ----
static uint32_t dehumLowSince = 0;
static bool tankAlerted = false;
static bool tankFull() {
  const int pw = PLUG_ENT(cfgAlertPlug());
  if (!isOn(S_ALERT) || isnan(num(pw))) { dehumLowSince = 0; return false; }
  if (num(pw) >= ALERT_WATTS) { dehumLowSince = 0; return false; }
  if (!dehumLowSince) dehumLowSince = millis();
  return millis() - dehumLowSince >= ALERT_MS;
}

// ---- switching ----
// The ONE path from a tap on a Controls row to haToggle(). Any protection against accidental
// switching (unlock / confirm) belongs here. haToggle() itself refuses anything but S_TILE1..8.
static void controlsRowTapped(int i, uint32_t ms) {
  if (ms < pendingUntil[i]) return;   // still waiting for the last command
  strlcpy(pendingFrom[i], ev[TILES[i].sw].s, sizeof pendingFrom[i]);
  pendingUntil[i] = ms + 20000;
  haToggle(TILES[i].sw);
  audioBeep(1200, 25, 25);
}

// ---- touch ----
static void onTouch(const TouchEvent &te, uint32_t ms) {
  if (menuIsOpen()) {
    menuTouched();
    if (te.type == TouchEvent::TAP) { menuTouch(te.x, te.y); audioBeep(1200, 20, 20); }
    return;
  }
  if (te.type == TouchEvent::LONG && titleRect().hit(te.x, te.y)) {
    menuOpen();
    audioBeep(900, 60, 30);
  } else if (te.type == TouchEvent::SWIPE_LEFT || (te.type == TouchEvent::TAP && te.x >= UI_W - EDGE)) {
    page = (page + 1) % NPAGES;
  } else if (te.type == TouchEvent::SWIPE_RIGHT || (te.type == TouchEvent::TAP && te.x < EDGE)) {
    page = (page + NPAGES - 1) % NPAGES;
  } else if (te.type == TouchEvent::TAP && page == CONTROLS) {
    // edge taps were handled above, so they never reach a row
    for (int i = 0; i < NTILES; i++)
      if (*TILES[i].name && tileRect(i).hit(te.x, te.y)) controlsRowTapped(i, ms);
  } else if (te.type == TouchEvent::LONG && HIVE_BOOST && page == HOME && heatingRect().hit(te.x, te.y)) {
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

// ---- main ----
static uint32_t wakeUntil = 0;
static int lastBand = -1;

void setup() {
  Serial.begin(115200);
  delay(200);
  cfgBegin();
  loadTiles();
  boardSetRotation(cfgRotation());
  if (!boardInit()) { Serial.println("boardInit failed"); for (;;) delay(1000); }
  cv = boardCanvas();
  uiApplyBacklight();
  touchBegin();
  audioBegin();
  setenv("TZ", TZ_RULE, 1); tzset();
  netBegin();
  configTzTime(TZ_RULE, "pool.ntp.org", "time.google.com");
  haBegin();
  Serial.println("cyd-home-panel up");
}

void loop() {
  netLoop();
  static uint32_t lastDraw = 0, lastWifiLog = 0;
  static bool dirty = true;
  uint32_t ms = millis();

  if (ms - lastWifiLog > 10000) {
    lastWifiLog = ms;
    Serial.printf("wifi %d ip %s rssi %d ha %d heap %u psram %u rot %d\n", WiFi.status(), WiFi.localIP().toString().c_str(),
                  WiFi.RSSI(), haOnline(), ESP.getFreeHeap(), ESP.getFreePsram(), boardRotation());
  }

  time_t now = time(nullptr);
  struct tm lt; localtime_r(&now, &lt);
  bool timeOk = now > 1735689600;
  bool night = timeOk && (lt.tm_hour >= NIGHT_FROM || lt.tm_hour < NIGHT_TO);

  // touch (the first touch while dimmed only wakes the screen)
  TouchEvent te = touchPoll();
  if (te.type != TouchEvent::NONE) {
    bool wasDim = dimmed;
    wakeUntil = ms + 30000;
    dirty = true;
    if (!wasDim) onTouch(te, ms);
    else if (menuIsOpen()) menuTouched();
  }
  menuTick();

  // backlight
  bool wantDim = night && ms > wakeUntil;
  if (wantDim != dimmed) { dimmed = wantDim; uiApplyBacklight(); dirty = true; }

  // serial test helpers: 1/2/3 page, r rotate, m menu, i system info, s screenshot
  int sc = Serial.available() ? Serial.read() : -1;
  if (sc >= '1' && sc <= '3') { menuClose(); page = sc - '1'; dirty = true; }
  else if (sc == 'r') { uiRotateNext(); dirty = true; }
  else if (sc == 'm') { if (menuIsOpen()) menuClose(); else menuOpen(); dirty = true; }
  else if (sc == 'i') { menuOpen(true); dirty = true; }
  if (sc == 's') dirty = true;   // draw a fresh frame, then dump it below

  if (!dirty && ms - lastDraw < 1000) { delay(5); return; }
  dirty = false;
  lastDraw = ms;
  haSnapshot(ev);

  // pending tiles clear when the state flips
  for (int i = 0; i < NTILES; i++)
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

  if (menuIsOpen()) menuDraw();
  else {
    cv->fillScreen(BG);
    drawStatus(lt, timeOk);
    if (page == HOME) drawHome(lt, timeOk);
    else if (page == ENERGY) drawEnergy(now);
    else drawControls();
    drawNav();
    drawOverlays(tank);
  }
  boardPresent();

  // 's' dumps the canvas (RGB565 little-endian) for tools/screenshot.py
  if (sc == 's') {
    Serial.printf("\nSHOT %d %d\n", UI_W, UI_H);
    Serial.write((const uint8_t *)cv->getBuffer(), (size_t)UI_W * UI_H * 2);
    Serial.println("\nENDSHOT");
  }
}
