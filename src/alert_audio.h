// Speaker alerts for the 3.5" ESP32-S3 board: ES8311 codec + I2S + PA enable on GPIO1.
// Assumes Wire is ALREADY begun (SDA38/SCL39 @ 400 kHz, shared with touch 0x55); this module never calls Wire.begin().
#pragma once
#include <Arduino.h>

enum AlertKind { PEAK_CHIME, APPLIANCE_ALERT };   // peak price starts; appliance stopped

// Pins: I2C/I2S PROVEN (https://github.com/vitorhpsousa/DPFSentinel-ESP32-S3/blob/main/docs/cyd35-hardware-notes.md).
#define AUDIO_PIN_MCLK 17
#define AUDIO_PIN_BCLK 18
#define AUDIO_PIN_WS   21
#define AUDIO_PIN_DOUT 15
#define AUDIO_PIN_DIN  16
#define AUDIO_PIN_PA    1
#ifndef AUDIO_PA_ACTIVE_HIGH
#define AUDIO_PA_ACTIVE_HIGH 0   // VERIFIED by ear 2026-09-26: the amplifier enable on GPIO1 is active LOW
#endif
#define AUDIO_ES8311_ADDR 0x18  // PROVEN
#ifndef AUDIO_SQUARE
#define AUDIO_SQUARE 1   // 1 = 8-bit square-wave voice, 0 = smooth sine
#endif
#define AUDIO_MAX_VOLUME_PCT 60 // hard cap applied to every request; default volume is also this

bool audioBegin();  // codec init + I2S + task; false if codec doesn't ACK or I2S fails. PA stays off until a tone plays.
void audioBeep(uint16_t hz, uint16_t ms, uint8_t volumePct);  // non-blocking, queued
void audioAlert(AlertKind kind);                              // non-blocking, queued
