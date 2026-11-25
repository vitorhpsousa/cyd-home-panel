#pragma once
// Shared UI state, palette and layout for the three pages (ui_pages.cpp) and the hidden menu (ui_menu.cpp).
// Every position is computed from the current canvas size (UI_W x UI_H), so all four rotations work:
// landscape 480x320 (rotation 1/3) and portrait 320x480 (rotation 0/2).
#include <Arduino.h>
#include <time.h>
#include "board.h"
#include "aatext.h"
#include "ha.h"

extern GFXcanvas16 *cv;           // canvas for the current rotation (refresh with uiApplyRotation())
extern EntVal ev[E_COUNT];        // snapshot of HA values, taken once per frame

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
constexpr uint16_t BG = rgb(10, 14, 22), CARD = rgb(26, 33, 46), CARD_HI = rgb(40, 50, 68),
                   TXT = 0xFFFF, DIM = rgb(140, 152, 170), FAINT = rgb(62, 72, 90), GREEN = rgb(60, 205, 120),
                   AMBER = rgb(245, 170, 40), RED = rgb(235, 72, 72), BLUE = rgb(90, 165, 255),
                   ORANGE = rgb(255, 128, 40), ONFILL = rgb(250, 190, 60);

struct Rect {
  int x, y, w, h;
  bool hit(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

enum Page { HOME, ENERGY, CONTROLS, NPAGES };
extern int page;
extern const char *const PAGE_NAMES[NPAGES];

// ---- layout ----
// Pages change only by swiping. The side margins (EDGE px) stay clear of cards so a swipe is easy to start.
// The status bar is on top, the page dots at the bottom.
constexpr int EDGE = 44;          // side margin kept clear of cards (easy to grab for a swipe)
constexpr int STATUS_H = 30;      // status bar (0..27 text, 27..28 rate-coloured rule)
constexpr int DOTS_H = 20;        // page dots at the bottom
inline int contentL() { return 10; }
inline int contentR() { return UI_W - 10; }
inline int contentW() { return contentR() - contentL(); }
inline int contentBottom() { return UI_H - DOTS_H; }
constexpr int CONTROLS_FOOT_H = 52;   // under the Controls rows: the lock bar (42 px) + gaps
Rect titleRect();                 // page name in the status bar: long press opens the menu
Rect heatingRect();               // Home heating card: long press = Hive boost
Rect tileRect(int i);             // Controls row for switch i (0..NTILES-1); w = 0 if unused
Rect controlsBarRect();           // lock bar at the bottom of the Controls page (clear of the side margins)
Rect boostCardRect();             // "Boost heating?" confirm card
Rect boostButtonRect(bool start); // its [Start boost] / [Cancel] buttons
Rect alertBannerRect();           // red "appliance stopped" banner: a tap hides it until the alert clears

// ---- control tiles ----
struct Tile { const char *name; int sw; int pw; };
extern Tile TILES[NTILES];
extern uint32_t pendingUntil[NTILES];
extern char pendingFrom[NTILES][8];   // state when tapped; while pending the row shows the opposite

// ---- protection against accidental touches (state kept by main.cpp, drawn by ui_pages.cpp) ----
// The Controls page opens locked; a tap on the bar unlocks it for 25 s (each switch action restarts
// the timer). Rows marked confirm-off need a second tap within 3 s to turn OFF. Holding the Heating
// card asks "Boost heating?" with Cancel on the side where the finger was.
struct UiGuard {
  uint32_t unlockedUntil;   // 0 = locked
  uint32_t barFlashUntil;   // pulse the bar after a tap on a locked row
  int confirmRow;           // row waiting for its second (OFF) tap, -1 = none
  uint32_t confirmUntil;
  bool boostAsk;            // boost confirm card open
  bool boostCancelLeft;     // Cancel button on the left (finger was on the left half)
  float boostTarget;
  uint32_t boostAskUntil;
};
extern UiGuard guard;
constexpr uint32_t UNLOCK_MS = 25000, CONFIRM_MS = 3000, BOOST_ASK_MS = 8000;
inline bool controlsUnlocked() { return guard.unlockedUntil && (int32_t)(guard.unlockedUntil - millis()) > 0; }

// ---- helpers ----
bool isOn(int e);
float num(int e);
uint16_t rateCol(float r);
const char *rateName(float r);
void fmtPower(char *b, size_t n, float w);
void showToast(const char *s);
bool toastShowing();              // a toast is up (it covers the alert banner)
// Cuts s (in place) with "…" until it fits maxW in font f.
void fitText(char *s, size_t n, const AAFont &f, int maxW);

// ---- drawing ----
void drawStatus(const struct tm &lt, bool timeOk);
void drawNav();                   // page dots
void drawHome(const struct tm &lt, bool timeOk);
void drawEnergy(time_t now);
void drawControls();
void drawOverlays(bool tank);     // toast / tank alert banner (tank = show it), boost confirm card

// ---- hidden menu (ui_menu.cpp) ----
bool menuIsOpen();
void menuOpen(bool info = false); // info = jump straight to the System info view
void menuClose();
void menuDraw();
void menuTouch(int x, int y);     // a tap while the menu is open
void menuTick();                  // closes the menu after 30 s without a touch
void menuTouched();               // any touch while open (keeps it open)
void uiRotateNext();              // +90 degrees, saved and applied immediately
void uiApplyBacklight();          // day brightness from config, or the night level while dimmed
extern bool dimmed;
