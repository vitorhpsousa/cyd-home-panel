// Light sleep with the display off (DISPOFF, not SLPIN: the power test showed the touch controller,
// which lives in the display driver, stops reporting taps in SLPIN but keeps working in DISPOFF).
// Touch INT (GPIO 47) idles high and goes low on a touch; it wakes the chip from light sleep.
#include "sleep.h"
#include "board.h"
#include "net.h"
#include <WiFi.h>
#include "esp_sleep.h"
#include "driver/gpio.h"

static const gpio_num_t TINT = GPIO_NUM_47;
static const int LED = 40;

// A pending touch report keeps INT low; reading the controller clears it. Waits for INT to go high
// (finger off), up to 2 s.
static bool intIdle() {
  uint32_t t0 = millis();
  while (millis() - t0 < 2000) {
    int16_t x, y; bool p = false;
    boardTouchRead(x, y, p);
    if (boardTouchIntLevel() == 1) return true;
    delay(30);
  }
  return false;
}

bool sleepUntilTouch() {
  if (!intIdle()) return false;
  boardSetBacklight(0);
  neopixelWrite(LED, 0, 0, 0);
  boardDisplayMode(1);   // DISPOFF: panel dark, touch still scanning
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("sleep: asleep");
  Serial.flush();
  gpio_wakeup_enable(TINT, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  uint32_t spurious = 0;
  for (;;) {
    esp_light_sleep_start();
    // Woken by INT: make sure it was a finger (the controller can also pulse INT on its own).
    bool pressed = false;
    for (int i = 0; i < 6 && !pressed; i++) {
      int16_t x, y;
      if (!boardTouchRead(x, y, pressed)) pressed = false;
      if (!pressed) delay(15);
    }
    if (pressed) break;
    spurious++;
    if (!intIdle()) break;   // INT stuck low: wake up rather than spin with the screen dark
  }
  gpio_wakeup_disable(TINT);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  boardDisplayMode(0);
  netBegin();
  Serial.printf("sleep: awake (%u false wake-ups)\n", (unsigned)spurious);
  return true;
}
