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
// TILE5..8 are optional in panel_settings.h (older copies stop at 4).
#ifndef TILE5_NAME
#define TILE5_NAME ""
#define TILE5_SWITCH ""
#define TILE5_PLUG 0
#endif
#ifndef TILE6_NAME
#define TILE6_NAME ""
#define TILE6_SWITCH ""
#define TILE6_PLUG 0
#endif
#ifndef TILE7_NAME
#define TILE7_NAME ""
#define TILE7_SWITCH ""
#define TILE7_PLUG 0
#endif
#ifndef TILE8_NAME
#define TILE8_NAME ""
#define TILE8_SWITCH ""
#define TILE8_PLUG 0
#endif
#ifndef TILE1_CONFIRM_OFF
#define TILE1_CONFIRM_OFF 0
#endif
#ifndef TILE2_CONFIRM_OFF
#define TILE2_CONFIRM_OFF 0
#endif
#ifndef TILE3_CONFIRM_OFF
#define TILE3_CONFIRM_OFF 0
#endif
#ifndef TILE4_CONFIRM_OFF
#define TILE4_CONFIRM_OFF 0
#endif
#ifndef TILE5_CONFIRM_OFF
#define TILE5_CONFIRM_OFF 0
#endif
#ifndef TILE6_CONFIRM_OFF
#define TILE6_CONFIRM_OFF 0
#endif
#ifndef TILE7_CONFIRM_OFF
#define TILE7_CONFIRM_OFF 0
#endif
#ifndef TILE8_CONFIRM_OFF
#define TILE8_CONFIRM_OFF 0
#endif
static const bool TILE_CONFIRM[NTILES] = {TILE1_CONFIRM_OFF, TILE2_CONFIRM_OFF, TILE3_CONFIRM_OFF, TILE4_CONFIRM_OFF,
                                          TILE5_CONFIRM_OFF, TILE6_CONFIRM_OFF, TILE7_CONFIRM_OFF, TILE8_CONFIRM_OFF};
bool cfgTileConfirmOff(int i) { return i >= 0 && i < NTILES && TILE_CONFIRM[i]; }
static const char *const TILE_NAMES[NTILES] = {TILE1_NAME, TILE2_NAME, TILE3_NAME, TILE4_NAME,
                                               TILE5_NAME, TILE6_NAME, TILE7_NAME, TILE8_NAME};
static const int TILE_PLUGS[NTILES] = {TILE1_PLUG, TILE2_PLUG, TILE3_PLUG, TILE4_PLUG,
                                       TILE5_PLUG, TILE6_PLUG, TILE7_PLUG, TILE8_PLUG};
const char *cfgPlugName(int i) { return (i >= 0 && i < 10) ? PLUG_NAMES[i] : ""; }
const char *cfgTileName(int i) { return (i >= 0 && i < NTILES) ? TILE_NAMES[i] : ""; }
int cfgTilePlug(int i) { return (i >= 0 && i < NTILES) ? TILE_PLUGS[i] : 0; }
int cfgAlertPlug() { return ALERT_PLUG; }

// Log page settings are optional (older panel_settings.h copies don't have them).
#ifndef SHOWER_SCRIPT
#define SHOWER_SCRIPT ""
#endif
#ifndef SHOWER_END_SCRIPT
#define SHOWER_END_SCRIPT ""
#endif
#ifndef SHOWER_MINUTES
#define SHOWER_MINUTES 10
#endif
#ifndef FLOW_SELECT
#define FLOW_SELECT ""
#endif
#ifndef FLOW_OPTIONS
#define FLOW_OPTIONS ""
#endif
#ifndef LOG_BUTTONS
#define LOG_BUTTONS ""
#endif
#ifndef LOG_NAME
#define LOG_NAME "House log"
#endif
const char *cfgShowerEndScript() { return SHOWER_END_SCRIPT; }
int cfgShowerMinutes() { return SHOWER_MINUTES; }
const char *cfgFlowOptions() { return FLOW_OPTIONS; }
bool cfgFlowOptionValid(const char *o) {   // o must be one whole item of FLOW_OPTIONS
  size_t n = strlen(o);
  if (!n) return false;
  for (const char *p = FLOW_OPTIONS; *p;) {
    const char *e = strchr(p, ',');
    size_t len = e ? (size_t)(e - p) : strlen(p);
    if (len == n && !strncmp(p, o, n)) return true;
    if (!e) break;
    p = e + 1;
  }
  return false;
}
const char *cfgLogButtons() { return LOG_BUTTONS; }
const char *cfgLogName() { return LOG_NAME; }
#ifndef SLEEP_AFTER_S
#define SLEEP_AFTER_S 0
#endif
int cfgSleepAfterS() { return SLEEP_AFTER_S; }

static const char *const IDS[E_COUNT] = {
  ENT_INDOOR_TEMP, ENT_OUTDOOR_TEMP, ENT_HEATING, ENT_WEATHER,
  ENT_RATE, ENT_NEXT_RATE, ENT_TODAY_KWH, ENT_TODAY_COST,
  PLUG1_COST, PLUG2_COST, PLUG3_COST, PLUG4_COST, PLUG5_COST,
  PLUG6_COST, PLUG7_COST, PLUG8_COST, PLUG9_COST, PLUG10_COST,
  ENT_DEMAND,
  PLUG1_POWER, PLUG2_POWER, PLUG3_POWER, PLUG4_POWER, PLUG5_POWER,
  PLUG6_POWER, PLUG7_POWER, PLUG8_POWER, PLUG9_POWER, PLUG10_POWER,
  TILE1_SWITCH, TILE2_SWITCH, TILE3_SWITCH, TILE4_SWITCH,
  TILE5_SWITCH, TILE6_SWITCH, TILE7_SWITCH, TILE8_SWITCH,
  ALERT_SWITCH,
  FLOW_SELECT, SHOWER_SCRIPT, "",   // X_LOG is not an entity
};
const char *cfgEntity(int e) { return (e >= 0 && e < E_COUNT) ? IDS[e] : ""; }
