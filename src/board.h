#pragma once
// 3.5" 320x480 ST77922 QSPI ESP32-S3 board (CYD-style). Pins proven from the factory firmware,
// see https://github.com/vitorhpsousa/DPFSentinel-ESP32-S3/blob/main/docs/cyd35-hardware-notes.md. The display has no library: raw IDF spi_master QSPI driver +
// an Adafruit GFXcanvas16 (PSRAM) that is pushed to the panel by boardPresent().
#include <Arduino.h>
#include <Adafruit_GFX.h>

#define LCD_W 320            // physical panel (portrait) size
#define LCD_H 480
// Rotation is chosen at run time: the number of 90-degree clockwise turns of the picture relative to
// the panel's own portrait orientation. 0 = portrait (320x480), 1 = landscape (480x320, the original
// layout), 2 = portrait upside down, 3 = landscape upside down. There is one canvas per shape;
// boardCanvas() returns the one for the current rotation. boardPresent() rotates it into the panel
// buffer and boardTouchRead() maps raw touches back into canvas coordinates.
#define UI_W boardUiW()
#define UI_H boardUiH()
#define BOARD_PIN_BL 41      // backlight, active high, LEDC PWM
#define BOARD_PIN_SDA 38
#define BOARD_PIN_SCL 39

// Starts I2C (400 kHz), the panel, the touch controller (reset after the LCD init) and the backlight.
// Returns false if the canvas/buffers could not be allocated.
bool boardInit();
GFXcanvas16 *boardCanvas();      // UI_W x UI_H canvas for the current rotation; draw here, then boardPresent()
void boardSetRotation(uint8_t r);   // 0..3, see above; takes effect on the next boardCanvas()/boardPresent()
uint8_t boardRotation();
inline bool boardPortrait() { return (boardRotation() & 1) == 0; }
int boardUiW();                  // 480 landscape, 320 portrait
int boardUiH();                  // 320 landscape, 480 portrait
void boardPresent();             // push the whole canvas to the panel (~20 ms; call from ONE task only)
void boardSetBacklight(uint8_t v);   // 0..255
// Reads the touch controller with one 39-byte burst. Returns false on I2C error.
// pressed=false means no finger; x 0..UI_W-1, y 0..UI_H-1 (canvas coordinates for the current rotation) otherwise.
bool boardTouchRead(int16_t &x, int16_t &y, bool &pressed);
