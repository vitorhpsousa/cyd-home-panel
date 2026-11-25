// CYD home panel: 3 pages (Home, Energy, Controls) fed by Home Assistant. Swipe left/right to change
// page; hold the page name for the hidden menu. Drawing lives in ui_pages.cpp / ui_menu.cpp.
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
char pendingFrom[NTILES][8];

void uiApplyBacklight() { boardSetBacklight(dimmed ? BL_NIGHT : cfgBrightness()); }

// ---- alerts ----
static uint32_t dehumLowSince = 0;
static bool tankAlerted = false;
static bool tankDismissed = false;   // banner tapped away: hidden until the alert clears, then it re-arms
static bool tankBannerUp = false;    // the banner was on screen in the last frame (taps check this)
static bool tankLook = false;        // serial 'a': show the banner for screenshots (UI only)
static bool tankFull() {
  const int pw = PLUG_ENT(cfgAlertPlug());
  if (!isOn(S_ALERT) || isnan(num(pw))) { dehumLowSince = 0; return false; }
  if (num(pw) >= ALERT_WATTS) { dehumLowSince = 0; return false; }
  if (!dehumLowSince) dehumLowSince = millis();
  return millis() - dehumLowSince >= ALERT_MS;
}

// ---- switching (with the accidental-touch guard) ----
UiGuard guard = {0, 0, -1, 0, false, false, 21, 0};
static void lockControls() { guard.unlockedUntil = 0; guard.confirmRow = -1; }

// The ONE path from a tap on a Controls row to haSet(). The page must be unlocked; rows marked
// confirm-off need a second tap within 3 s to turn OFF. haSet() itself refuses anything but S_TILE1..8.
static void controlsRowTapped(int i, uint32_t ms) {
  if (!controlsUnlocked()) { guard.barFlashUntil = ms + 700; audioBeep(500, 40, 20); return; }
  const int e = TILES[i].sw;
  if (!ev[e].ok || !ev[e].avail || ms < pendingUntil[i]) return;   // unknown state, or still sending
  guard.unlockedUntil = ms + UNLOCK_MS;                            // every action restarts the 25 s
  const bool on = isOn(e);
  if (on && cfgTileConfirmOff(i)) {
    bool armed = guard.confirmRow == i && (int32_t)(guard.confirmUntil - ms) > 0;
    if (!armed) { guard.confirmRow = i; guard.confirmUntil = ms + CONFIRM_MS; audioBeep(700, 40, 25); return; }
  }
  guard.confirmRow = -1;
  strlcpy(pendingFrom[i], ev[e].s, sizeof pendingFrom[i]);
  if (!haSet(e, !on)) {   // offline, or a command for it is still running: say so, don't queue it
    showToast(haOnline() ? "Still sending - try again in a moment" : "Home Assistant offline - not sent");
    audioBeep(400, 80, 25);
    return;
  }
  pendingUntil[i] = ms + 45000;   // shown as switched straight away; cleared when HA reports the new state
  audioBeep(1200, 25, 25);
}

static void boostAsk(int fingerX, uint32_t ms) {
  const EntVal &c = ev[E_CLIMATE];
  float cur = isnan(num(E_TIN)) ? c.curTemp : num(E_TIN);
  guard.boostTarget = isnan(cur) ? 21 : constrain(roundf(cur) + 1, 20, 25);
  guard.boostCancelLeft = fingerX < UI_W / 2;
  guard.boostAsk = true;
  guard.boostAskUntil = ms + BOOST_ASK_MS;
}

// ---- touch ----
// distance in px from (x, y) to the nearest point of r (0 when inside)
static int rectDist(const Rect &r, int x, int y) {
  int dx = x < r.x ? r.x - x : (x >= r.x + r.w ? x - (r.x + r.w - 1) : 0);
  int dy = y < r.y ? r.y - y : (y >= r.y + r.h ? y - (r.y + r.h - 1) : 0);
  return max(dx, dy);
}
static constexpr int TAP_SLOP = 24;

static void onTouch(const TouchEvent &te, uint32_t ms) {
  if (menuIsOpen()) {
    menuTouched();
    if (te.type == TouchEvent::TAP) { menuTouch(te.x, te.y); audioBeep(1200, 20, 20); }
    return;
  }
  if (guard.boostAsk) {   // the confirm card takes every touch: Start starts, anything else cancels
    guard.boostAsk = false;
    if (te.type == TouchEvent::TAP && boostButtonRect(true).hit(te.x, te.y)) {
      haBoost(guard.boostTarget, 30);
      char b[48]; snprintf(b, sizeof b, "Boost 30 min to %.0f\xC2\xB0" "C", guard.boostTarget);
      showToast(b);
      audioBeep(900, 60, 30);
    }
    return;
  }
  if (te.type == TouchEvent::TAP && tankBannerUp && rectDist(alertBannerRect(), te.x, te.y) <= 8) {
    tankDismissed = true;   // the tap only hides the banner; it never reaches a row underneath
    tankLook = false;
    audioBeep(1000, 25, 20);
    return;
  }
  if (te.type == TouchEvent::LONG && titleRect().hit(te.x, te.y)) {
    menuOpen();
    lockControls();
    audioBeep(900, 60, 30);
  } else if (te.type == TouchEvent::SWIPE_LEFT) {
    page = (page + 1) % NPAGES;
  } else if (te.type == TouchEvent::SWIPE_RIGHT) {
    page = (page + NPAGES - 1) % NPAGES;
  } else if (te.type == TouchEvent::TAP && page == CONTROLS) {
    // forgiving: a tap goes to the nearest row or the bar, if it is within TAP_SLOP px of it
    int best = -2, bestD = TAP_SLOP + 1;
    int d = rectDist(controlsBarRect(), te.x, te.y);
    if (d < bestD) { bestD = d; best = -1; }
    for (int i = 0; i < NTILES; i++)
      if (*TILES[i].name && (d = rectDist(tileRect(i), te.x, te.y)) < bestD) { bestD = d; best = i; }
    if (best == -1) {
      guard.unlockedUntil = ms + UNLOCK_MS;   // the bar only unlocks (a double tap must not re-lock); it locks by itself
      audioBeep(1000, 25, 20);
    } else if (best >= 0) controlsRowTapped(best, ms);
  } else if (te.type == TouchEvent::LONG && HIVE_BOOST && page == HOME && heatingRect().hit(te.x, te.y)) {
    if (!strcmp(ev[E_CLIMATE].preset, "boost")) { haBoostOff(); showToast("Stopping boost"); }   // stopping stays one step
    else boostAsk(te.x, ms);
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

  // guard housekeeping: leaving the page, the menu or dimming re-locks; timers run out
  static int lastPage = -1;
  if (page != CONTROLS || menuIsOpen() || dimmed) {
    if (guard.unlockedUntil || guard.confirmRow >= 0) { lockControls(); dirty = true; }
  }
  if (page != lastPage) { lockControls(); guard.boostAsk = false; lastPage = page; }
  if (guard.unlockedUntil && !controlsUnlocked()) { lockControls(); dirty = true; }
  if (guard.confirmRow >= 0 && (int32_t)(guard.confirmUntil - ms) <= 0) { guard.confirmRow = -1; dirty = true; }
  if (guard.boostAsk && ((int32_t)(guard.boostAskUntil - ms) <= 0 || dimmed || menuIsOpen())) { guard.boostAsk = false; dirty = true; }

  // serial test helpers: 1/2/3 page, r rotate, m menu, i system info, s screenshot.
  // UI-state only (they never call haSet/haBoost): u unlock/lock Controls, c show the confirm-off
  // state on the first confirm-off row, b show the boost confirm card, a show/hide the alert banner.
  int sc = Serial.available() ? Serial.read() : -1;
  if (sc >= '1' && sc <= '3') { menuClose(); page = sc - '1'; dirty = true; }
  else if (sc == 'r') { uiRotateNext(); dirty = true; }
  else if (sc == 'm') { if (menuIsOpen()) menuClose(); else menuOpen(); dirty = true; }
  else if (sc == 'i') { menuOpen(true); dirty = true; }
  else if (sc == 'u') { if (controlsUnlocked()) lockControls(); else guard.unlockedUntil = ms + UNLOCK_MS; dirty = true; }
  else if (sc == 'c') {
    for (int i = 0; i < NTILES; i++)
      if (*TILES[i].name && cfgTileConfirmOff(i)) { guard.unlockedUntil = ms + UNLOCK_MS; guard.confirmRow = i; guard.confirmUntil = ms + CONFIRM_MS; break; }
    dirty = true;
  }
  else if (sc == 'b') { boostAsk(UI_W - 1, ms); dirty = true; }
  else if (sc == 'a') { tankLook = !tankLook; dirty = true; }
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
  if (band == 2 && lastBand == 1 && !night) { audioAlert(PEAK_CHIME); showToast(PEAK_MESSAGE); }
  if (band >= 0) lastBand = band;

  bool tank = tankFull();
  if (tank && !tankAlerted) { tankAlerted = true; if (!night) audioAlert(APPLIANCE_ALERT); }
  if (!tank) { tankAlerted = false; tankDismissed = false; }
  const bool showTank = (tank && !tankDismissed) || tankLook;
  tankBannerUp = showTank && !toastShowing() && !menuIsOpen();

  if (menuIsOpen()) menuDraw();
  else {
    cv->fillScreen(BG);
    drawStatus(lt, timeOk);
    if (page == HOME) drawHome(lt, timeOk);
    else if (page == ENERGY) drawEnergy(now);
    else drawControls();
    drawNav();
    drawOverlays(showTank);
  }
  boardPresent();

  // 's' dumps the canvas (RGB565 little-endian) for tools/screenshot.py
  if (sc == 's') {
    Serial.printf("\nSHOT %d %d\n", UI_W, UI_H);
    Serial.write((const uint8_t *)cv->getBuffer(), (size_t)UI_W * UI_H * 2);
    Serial.println("\nENDSHOT");
  }
}
