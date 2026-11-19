#include "touch.h"
#include "board.h"

static const uint32_t POLL_MS = 20;          // ~50 Hz
static const uint32_t LONG_MS = 700;         // held still this long = long press
static const uint32_t TAP_MIN_MS = 60;       // shorter = a one-sample ghost blip, ignored
static const uint32_t TAP_MAX_MS = 600;
static const uint32_t UP_DEBOUNCE_MS = 60;   // finger absent this long = released
static const int STILL_MAX = 20;             // tap / long press: never further than this from touch-down

static uint32_t lastPoll = 0, lastActivity = 0, downAt = 0, lastSeen = 0;
static bool down = false, longFired = false;
static int16_t sx, sy, lx, ly;
static int maxDist = 0;   // furthest the finger got from the touch-down point (Chebyshev, px)

bool touchBegin() { lastActivity = millis(); return true; }
uint32_t touchLastActivityMs() { return lastActivity; }

// Tap = 60-600 ms touch that never moved more than 20 px from where it started; long = held within
// 20 px for 0.7 s; swipe = mostly-horizontal drag > 80 px (end point). Tracking the furthest
// distance (not just the end point) stops circular wiping from counting as a tap.
TouchEvent touchPoll() {
  TouchEvent ev = {TouchEvent::NONE, 0, 0};
  uint32_t now = millis();
  if (now - lastPoll < POLL_MS) return ev;
  lastPoll = now;
  int16_t x = 0, y = 0;
  bool pressed = false;
  if (!boardTouchRead(x, y, pressed)) return ev;
  if (pressed) {
    lastActivity = lastSeen = now;
    if (!down) { down = true; longFired = false; downAt = now; sx = lx = x; sy = ly = y; maxDist = 0; }
    else { lx = x; ly = y; maxDist = max(maxDist, max(abs(lx - sx), abs(ly - sy))); }
    // Long press: held still for 0.7 s -> one LONG event while still down; release then produces nothing.
    if (!longFired && now - downAt >= LONG_MS && maxDist <= STILL_MAX) { longFired = true; ev = {TouchEvent::LONG, sx, sy}; }
    return ev;
  }
  if (!down || now - lastSeen < UP_DEBOUNCE_MS) return ev;
  down = false;
  lastActivity = now;
  if (longFired) return ev;
  int dx = lx - sx, dy = ly - sy;
  uint32_t held = lastSeen - downAt;
  if (maxDist <= STILL_MAX) { if (held >= TAP_MIN_MS && held <= TAP_MAX_MS) ev = {TouchEvent::TAP, sx, sy}; }
  else if (abs(dx) > 80 && abs(dx) > 2 * abs(dy)) ev = {dx < 0 ? TouchEvent::SWIPE_LEFT : TouchEvent::SWIPE_RIGHT, sx, sy};
  return ev;
}
