#pragma once
#include <Arduino.h>

struct TouchEvent {
  enum Type { NONE, TAP, LONG, SWIPE_LEFT, SWIPE_RIGHT } type;
  int16_t x, y;  // touch-down point (canvas coordinates for the current rotation)
};

bool touchBegin();               // board touch is set up by boardInit(); this only resets the state machine
TouchEvent touchPoll();          // non-blocking; call from the UI task
uint32_t touchLastActivityMs();  // millis() of last touch down/move/up
