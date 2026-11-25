#pragma once
// Runtime settings: everything comes from panel_settings.h, except rotation and brightness, which
// the hidden menu changes and the board saves (NVS) so they survive a restart.
// Everything else in the firmware reads settings through these functions, never the macros directly.
#include <Arduino.h>

void cfgBegin();                       // load rotation/brightness (call first in setup())

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
const char *cfgTileName(int i);        // i = 0..NTILES-1 (8), "" = unused
int cfgTilePlug(int i);                // i = 0..NTILES-1
bool cfgTileConfirmOff(int i);         // switching it OFF needs a second tap (e.g. a desk with a PC)
int cfgAlertPlug();

// Log page (all optional; the page is hidden when none of them is set)
const char *cfgShowerEndScript();      // script that ends the shower pause early
int cfgShowerMinutes();                // pause length, only for the "until" time on screen
const char *cfgFlowOptions();          // "65,70,75": the flow buttons, options of FLOW_SELECT
bool cfgFlowOptionValid(const char *o);
const char *cfgLogButtons();           // "1 hob;2 hobs;Sink/hot water": '/' starts the small second line
const char *cfgLogName();              // name on the logbook lines
int cfgSleepAfterS();                  // battery saving: sleep after this many idle seconds, 0 = never

// Entity IDs, indexed by EntId (ha.h). "" = not used.
const char *cfgEntity(int e);
