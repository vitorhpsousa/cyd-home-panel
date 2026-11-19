// Settings come from panel_settings.h; only rotation and brightness (changed in the hidden menu)
// are saved on the board, in NVS, and survive a restart.
#include "config.h"
#include "settings_check.h"
#include "ha.h"
#include <Preferences.h>

static uint8_t s_rot = 1, s_bl = 255;
static Preferences prefs;
void cfgBegin() {
  prefs.begin("panel", false);
  s_rot = prefs.getUChar("rot", 1) & 3;
  s_bl = prefs.getUChar("bl", 255);
  if (s_bl < 10) s_bl = 10;
}
const char *cfgWifiSsid() { return WIFI_SSID; }
const char *cfgWifiPass() { return WIFI_PASS; }
const char *cfgHaHost() { return HA_HOST; }
uint16_t cfgHaPort() { return HA_PORT; }
const char *cfgHaToken() { return HA_TOKEN; }
uint8_t cfgRotation() { return s_rot; }
void cfgSetRotation(uint8_t r) {   // written only when it changes (flash wear)
  r &= 3;
  if (r != s_rot) { s_rot = r; prefs.putUChar("rot", r); }
}
uint8_t cfgBrightness() { return s_bl; }
void cfgSetBrightness(uint8_t v) {
  if (v < 10) v = 10;
  if (v != s_bl) { s_bl = v; prefs.putUChar("bl", v); }
}

static const char *const PLUG_NAMES[10] = {PLUG1_NAME, PLUG2_NAME, PLUG3_NAME, PLUG4_NAME, PLUG5_NAME,
                                           PLUG6_NAME, PLUG7_NAME, PLUG8_NAME, PLUG9_NAME, PLUG10_NAME};
static const char *const TILE_NAMES[4] = {TILE1_NAME, TILE2_NAME, TILE3_NAME, TILE4_NAME};
static const int TILE_PLUGS[4] = {TILE1_PLUG, TILE2_PLUG, TILE3_PLUG, TILE4_PLUG};
const char *cfgPlugName(int i) { return (i >= 0 && i < 10) ? PLUG_NAMES[i] : ""; }
const char *cfgTileName(int i) { return (i >= 0 && i < 4) ? TILE_NAMES[i] : ""; }
int cfgTilePlug(int i) { return (i >= 0 && i < 4) ? TILE_PLUGS[i] : 0; }
int cfgAlertPlug() { return ALERT_PLUG; }

static const char *const IDS[E_COUNT] = {
  ENT_INDOOR_TEMP, ENT_OUTDOOR_TEMP, ENT_HEATING, ENT_WEATHER,
  ENT_RATE, ENT_NEXT_RATE, ENT_TODAY_KWH, ENT_TODAY_COST,
  PLUG1_COST, PLUG2_COST, PLUG3_COST, PLUG4_COST, PLUG5_COST,
  PLUG6_COST, PLUG7_COST, PLUG8_COST, PLUG9_COST, PLUG10_COST,
  ENT_DEMAND,
  PLUG1_POWER, PLUG2_POWER, PLUG3_POWER, PLUG4_POWER, PLUG5_POWER,
  PLUG6_POWER, PLUG7_POWER, PLUG8_POWER, PLUG9_POWER, PLUG10_POWER,
  TILE1_SWITCH, TILE2_SWITCH, TILE3_SWITCH, TILE4_SWITCH,
  ALERT_SWITCH,
};
const char *cfgEntity(int e) { return (e >= 0 && e < E_COUNT) ? IDS[e] : ""; }
