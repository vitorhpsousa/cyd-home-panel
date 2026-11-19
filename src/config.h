#pragma once
// Runtime settings. Values the user saved on the board (setup page, hidden menu) win; anything not
// saved falls back to the defaults compiled in from panel_settings.h.
// Everything else in the firmware reads settings through these functions, never the macros directly.
#include <Arduino.h>

void cfgBegin();                       // load saved values (call first in setup())

// connection
const char *cfgWifiSsid();
const char *cfgWifiPass();
const char *cfgHaHost();
uint16_t cfgHaPort();
const char *cfgHaToken();

// display (saved immediately when changed)
uint8_t cfgRotation();                 // 0..3, see board.h
void cfgSetRotation(uint8_t r);
uint8_t cfgBrightness();               // day backlight 10..255
void cfgSetBrightness(uint8_t v);

// names and links between entities (plug numbers are 1..10, 0 = none)
const char *cfgPlugName(int i);        // i = 0..9, "" = unused
const char *cfgTileName(int i);        // i = 0..3, "" = unused
int cfgTilePlug(int i);                // i = 0..3
int cfgAlertPlug();

// Entity IDs, indexed by EntId (ha.h). "" = not used.
const char *cfgEntity(int e);
