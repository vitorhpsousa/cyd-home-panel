#pragma once
// Battery saving (SLEEP_AFTER_S in panel_settings.h, 0 = off): after that many seconds without a
// touch the panel turns the backlight and display off, switches Wi-Fi off and goes into light sleep.
// A tap wakes it (the touch INT pin goes low); that tap only wakes it, it never presses anything.
// Fresh data arrives a few seconds after waking, once Wi-Fi is back.
#include <Arduino.h>

// Sleeps until a real touch. Returns false straight away (without sleeping) if the touch pin is stuck
// low, so the caller can stay awake.
bool sleepUntilTouch();
