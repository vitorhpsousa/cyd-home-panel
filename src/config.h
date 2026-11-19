#pragma once
// Runtime settings. Values the user saved on the board (setup page, hidden menu) win; anything not
// saved falls back to the defaults compiled in from panel_settings.h.
// Everything else in the firmware reads settings through these functions, never the macros directly.
#include <Arduino.h>

// Thread safety: the strings returned below live in buffers loaded once by cfgBegin() and never change
// while the panel runs (saving on the setup page restarts the board), so any task may read them.
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

// ---- Setup-page support (used by net.cpp; the UI doesn't need these) -------------------------
// Every setting the setup page can change is a "field" with a short key (also its NVS key and its
// HTML form name). Saved values take effect after a restart, except the Wi-Fi name/password, which
// only net.cpp reads (same task as the setup page), so they are updated in RAM straight away.
enum CfgKind : uint8_t { CFG_TEXT, CFG_ENTITY, CFG_SWITCH, CFG_HOST, CFG_SSID, CFG_WPASS, CFG_TOKEN, CFG_NUM };
struct CfgFieldInfo {
  const char *key;
  CfgKind kind;
  bool secret;        // never sent to the browser (Wi-Fi password, HA token)
  uint16_t max;       // text: max length in bytes; number: max value
};
int cfgFieldCount();
bool cfgFieldInfo(int i, CfgFieldInfo *out);
int cfgFieldFind(const char *key);                  // -1 if unknown
void cfgFieldValue(int i, char *out, size_t n);     // current value as text; always "" for secret fields
bool cfgFieldHasValue(int i);                       // value is not empty (for secrets: "a value is saved")
// Checks a new value without saving it. nullptr = fine, else a short plain-English reason.
const char *cfgFieldCheck(int i, const char *text);
// Saves a checked value. Writes flash only if it differs from the current value.
bool cfgFieldSave(int i, const char *text);
void cfgResetToDefaults();                          // forget saved values (keeps rotation, brightness, hotspot password)
const char *cfgSetupPass();                         // setup hotspot password: random, made once per device, kept in NVS
