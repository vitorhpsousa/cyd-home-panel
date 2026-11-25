// Power test: six stages, 15 s each. Before each one the screen shows "Stage N" for 3 s and the LED
// blinks N times, so the draw can be read off a USB meter. In stages 3 and 4 (display off / asleep)
// a tap the touch controller still reports lights the screen for 1.5 s, to show a tap can wake the
// panel from that state. The touch INT pin level is sampled too (idle vs touched) for the wake-up pin.
#include "power_test.h"
#include "ui.h"
#include "net.h"
#include "alert_audio.h"
#include <WiFi.h>
#include "esp_sleep.h"

static const uint32_t STAGE_MS = 15000;
static const char *const STAGES[6] = {
  "1  Normal, full brightness",
  "2  Backlight off",
  "3  Display off (tap!)",
  "4  Display asleep (tap!)",
  "5  + Wi-Fi off, CPU 80 MHz",
  "6  + light sleep",
};

static void screen(const char *title, const char *l1, const char *l2 = "", const char *l3 = "") {
  cv->fillScreen(BG);
  aaDrawCentered(cv, UI_W / 2, UI_H / 2 - 70, title, aaB24, TXT);
  aaDrawCentered(cv, UI_W / 2, UI_H / 2 - 24, l1, aaR17, DIM);
  aaDrawCentered(cv, UI_W / 2, UI_H / 2 + 4, l2, aaR17, DIM);
  aaDrawCentered(cv, UI_W / 2, UI_H / 2 + 32, l3, aaR17, DIM);
  boardPresent();
}

// The RGB LED (GPIO 40) blinks the stage number in blue (beeps too, for boards with a speaker).
static const int LED = 40;
static void blinks(int n) {
  for (int i = 0; i < n; i++) {
    audioBeep(1000, 120, 40);
    neopixelWrite(LED, 0, 0, 60); delay(250);
    neopixelWrite(LED, 0, 0, 0); delay(300);
  }
}

// Before every stage: screen on for ~3 s with the stage name while the LED blinks its number,
// so each stage is easy to tell apart. Then the caller sets the stage's power state.
static void announce(int n) {
  char t[16]; snprintf(t, sizeof t, "Stage %d", n);
  boardDisplayMode(0);
  screen(t, STAGES[n - 1], n == 3 || n == 4 ? "Tap the dark screen: it should" : "Read the meter once it settles",
         n == 3 || n == 4 ? "light up for a moment (touch works)" : "");
  boardSetBacklight(255);
  uint32_t t0 = millis();
  blinks(n);
  while (millis() - t0 < 3000) delay(20);
  Serial.printf("PT stage %d\n", n);
}

// Holds a stage for STAGE_MS, polling touch every 20 ms and counting INT levels. With wakeDemo, a tap
// lights the screen for 1.5 s ("Tap seen") and the LED green, then the stage's dark state comes back.
static int idleHi, idleLo, touchHi, touchLo;
static int holdStage(int n, bool wakeDemo, int dispMode) {
  int taps = 0;
  bool was = false;
  uint32_t t0 = millis();
  while (millis() - t0 < STAGE_MS) {
    int lv = boardTouchIntLevel();
    int16_t x, y; bool pressed = false;
    if (boardTouchRead(x, y, pressed)) {
      if (pressed) { lv ? touchHi++ : touchLo++; } else { lv ? idleHi++ : idleLo++; }
      if (pressed && !was) {
        taps++;
        if (wakeDemo) {
          char t[40]; snprintf(t, sizeof t, "Tap seen in stage %d", n);
          char c[24]; snprintf(c, sizeof c, "taps so far: %d", taps);
          neopixelWrite(LED, 0, 60, 0);
          boardDisplayMode(0);
          screen(t, "Touch works while the screen is off", c);
          boardSetBacklight(255);
          delay(1500);
          boardSetBacklight(0);
          boardDisplayMode(dispMode);
          neopixelWrite(LED, 0, 0, 0);
          t0 += 1500 + 200;   // don't count the lit time as part of the stage
        }
      }
      was = pressed;
    }
    delay(20);
  }
  return taps;
}

static void darkStage(int dispMode) { boardSetBacklight(0); boardDisplayMode(dispMode); }

void powerTestRun() {
  char b[64], b2[64], b3[64];
  idleHi = idleLo = touchHi = touchLo = 0;
  Serial.println("PT start");
  neopixelWrite(LED, 0, 0, 0);
  screen("Power test", "6 stages, 15 s each. Before each one the", "screen shows its number for 3 s and the", "LED blinks it. Read the meter in between.");
  delay(8000);

  announce(1);                                  // 1: normal, full brightness, Wi-Fi on
  holdStage(1, false, 0);

  announce(2); darkStage(0);                    // 2: backlight off
  holdStage(2, false, 0);

  announce(3); darkStage(1);                    // 3: display off (controller running)
  int taps3 = holdStage(3, true, 1);

  announce(4); darkStage(2);                    // 4: display asleep
  int taps4 = holdStage(4, true, 2);

  announce(5); darkStage(2);                    // 5: + Wi-Fi off, CPU 80 MHz
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  setCpuFrequencyMhz(80);
  holdStage(5, false, 2);

  setCpuFrequencyMhz(240);
  announce(6); darkStage(2);                    // 6: + light sleep (no taps, no USB serial while asleep)
  Serial.flush();
  delay(200);
  setCpuFrequencyMhz(80);
  esp_sleep_enable_timer_wakeup((uint64_t)STAGE_MS * 1000ULL);
  esp_light_sleep_start();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

  // back to normal
  setCpuFrequencyMhz(240);
  boardDisplayMode(0);
  netBegin();
  audioBeep(700, 600, 40);
  const char *intIdle = idleHi > idleLo ? "high" : "low", *intTouch = touchHi + touchLo == 0 ? "-" : touchHi > touchLo ? "high" : "low";
  snprintf(b, sizeof b, "Taps seen: stage 3 = %d, stage 4 = %d", taps3, taps4);
  snprintf(b2, sizeof b2, "Touch pin: idle %s, touched %s", intIdle, intTouch);
  snprintf(b3, sizeof b3, "(idle %d/%d, touched %d/%d hi/lo)", idleHi, idleLo, touchHi, touchLo);
  for (int i = 0; i < 5; i++) { Serial.printf("PT done: %s | %s %s\n", b, b2, b3); delay(200); }
  screen("Power test done", b, b2, b3);
  uiApplyBacklight();
  neopixelWrite(LED, 0, 60, 0); delay(1500); neopixelWrite(LED, 0, 0, 0);   // green = finished
  // results stay up until a tap (or 2 minutes)
  uint32_t t0 = millis();
  while (millis() - t0 < 120000) {
    int16_t x, y; bool p = false;
    if (boardTouchRead(x, y, p) && p) break;
    delay(30);
  }
  delay(400);   // finger off before the normal touch handling resumes
}
