// Settings storage. Every value lives in NVS (namespace "panel") once the user saves it on the setup
// page; anything never saved comes from the compiled-in defaults in panel_settings.h.
// A key that exists in NVS wins, even if it holds "" (that is how a user blanks out a default).
#include "config.h"
#include "settings_check.h"
#include "ha.h"
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_random.h>

static const char *NS = "panel";

// Max lengths (bytes, without the terminating 0).
enum { L_SSID = 32, L_WPASS = 64, L_HOST = 63, L_TOKEN = 300, L_ENT = 100, L_NAME = 31 };

struct Store {
  char ssid[L_SSID + 1], wpass[L_WPASS + 1], host[L_HOST + 1], token[L_TOKEN + 1];
  char ent[E_COUNT][L_ENT + 1];
  char plugName[NPLUGS][L_NAME + 1];
  char tileName[4][L_NAME + 1];
  uint16_t port;
  uint8_t tilePlug[4], alertPlug;
  char apPass[12];
};
static Store *S = nullptr;   // in PSRAM (about 4.5 KB)
static volatile uint8_t s_rot = 1, s_bl = 255;

struct Field {
  char key[8];          // NVS keys may be up to 15 chars; ours are short
  CfgKind kind;
  uint16_t max;         // text: max bytes; number: max value
  void *ptr;            // char[max+1] for text, uint16_t* for port, uint8_t* for small numbers
  const char *defS;     // default text
  int defN;             // default number
  bool live;            // RAM copy updated on save (Wi-Fi only: read by the loop task alone)
};
static const int NFIELDS = 5 + E_COUNT + NPLUGS + 4 + 4 + 1;
static Field F[NFIELDS];
static int nF = 0;

static const char *const DEF_ENT[E_COUNT] = {
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
static const char *const DEF_PLUG_NAMES[NPLUGS] = {PLUG1_NAME, PLUG2_NAME, PLUG3_NAME, PLUG4_NAME, PLUG5_NAME,
                                                   PLUG6_NAME, PLUG7_NAME, PLUG8_NAME, PLUG9_NAME, PLUG10_NAME};
static const char *const DEF_TILE_NAMES[4] = {TILE1_NAME, TILE2_NAME, TILE3_NAME, TILE4_NAME};
static const int DEF_TILE_PLUGS[4] = {TILE1_PLUG, TILE2_PLUG, TILE3_PLUG, TILE4_PLUG};

// Stable NVS keys for the entities (indexed by EntId). Keep these if EntId is ever reordered.
static void entKey(int e, char *k) {
  static const char *const fixed[] = {"e_tin", "e_tout", "e_clim", "e_wthr", "e_rate", "e_next", "e_kwh", "e_cost"};
  if (e < 8) strcpy(k, fixed[e]);
  else if (e >= C_PLUG1 && e <= C_PLUG10) sprintf(k, "c_p%d", e - C_PLUG1 + 1);
  else if (e == E_DEMAND) strcpy(k, "e_dem");
  else if (e >= P_PLUG1 && e <= P_PLUG10) sprintf(k, "p_p%d", e - P_PLUG1 + 1);
  else if (e >= S_TILE1 && e <= S_TILE4) sprintf(k, "s_t%d", e - S_TILE1 + 1);
  else strcpy(k, "s_alert");
}

static void addText(const char *key, CfgKind kind, char *buf, uint16_t max, const char *def, bool live = false) {
  Field &f = F[nF++];
  strlcpy(f.key, key, sizeof f.key);
  f.kind = kind; f.max = max; f.ptr = buf; f.defS = def ? def : ""; f.defN = 0; f.live = live;
}
static void addNum(const char *key, void *ptr, uint16_t max, int def) {
  Field &f = F[nF++];
  strlcpy(f.key, key, sizeof f.key);
  f.kind = CFG_NUM; f.max = max; f.ptr = ptr; f.defS = ""; f.defN = def; f.live = false;
}
static bool isPort(const Field &f) { return f.ptr == &S->port; }

static void buildFields() {
  nF = 0;
  addText("ssid", CFG_SSID, S->ssid, L_SSID, WIFI_SSID, true);
  addText("wpass", CFG_WPASS, S->wpass, L_WPASS, WIFI_PASS, true);
  addText("host", CFG_HOST, S->host, L_HOST, HA_HOST);
  addNum("port", &S->port, 65535, HA_PORT);
  addText("token", CFG_TOKEN, S->token, L_TOKEN, HA_TOKEN);
  char k[8];
  for (int e = 0; e < E_COUNT; e++) {
    entKey(e, k);
    addText(k, (e >= S_TILE1 && e <= S_TILE4) ? CFG_SWITCH : CFG_ENTITY, S->ent[e], L_ENT, DEF_ENT[e]);
  }
  for (int i = 0; i < NPLUGS; i++) { sprintf(k, "pn%d", i + 1); addText(k, CFG_TEXT, S->plugName[i], L_NAME, DEF_PLUG_NAMES[i]); }
  for (int i = 0; i < 4; i++) { sprintf(k, "tn%d", i + 1); addText(k, CFG_TEXT, S->tileName[i], L_NAME, DEF_TILE_NAMES[i]); }
  for (int i = 0; i < 4; i++) { sprintf(k, "tp%d", i + 1); addNum(k, &S->tilePlug[i], NPLUGS, DEF_TILE_PLUGS[i]); }
  addNum("alertp", &S->alertPlug, NPLUGS, ALERT_PLUG);
}

static int clampNum(const Field &f, long v) {
  long lo = isPort(f) ? 1 : 0;
  return v < lo ? lo : v > f.max ? f.max : (int)v;
}
static void setNum(Field &f, int v) {
  if (isPort(f)) *(uint16_t *)f.ptr = (uint16_t)v; else *(uint8_t *)f.ptr = (uint8_t)v;
}
static int getNum(const Field &f) { return isPort(f) ? *(uint16_t *)f.ptr : *(uint8_t *)f.ptr; }

static void makeApPass(Preferences &p) {
  // 8 characters without look-alikes (0/O, 1/l/I): about 40 bits, typed on a phone without fuss.
  static const char AL[] = "abcdefghjkmnpqrstuvwxyz23456789";
  if (p.isKey("appass") && p.getString("appass", S->apPass, sizeof S->apPass) >= 8) return;
  for (int i = 0; i < 8; i++) S->apPass[i] = AL[esp_random() % (sizeof AL - 1)];
  S->apPass[8] = 0;
  p.putString("appass", S->apPass);
}

void cfgBegin() {
  if (S) return;
  S = (Store *)heap_caps_calloc(1, sizeof(Store), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!S) S = (Store *)calloc(1, sizeof(Store));
  buildFields();
  Preferences p;
  bool open = p.begin(NS, false);   // read-write so the namespace exists on first boot
  for (int i = 0; i < nF; i++) {
    Field &f = F[i];
    bool saved = open && p.isKey(f.key);
    if (f.kind == CFG_NUM) {
      int v = saved ? (isPort(f) ? p.getUShort(f.key, f.defN) : p.getUChar(f.key, f.defN)) : f.defN;
      setNum(f, clampNum(f, v));
    } else {
      char *buf = (char *)f.ptr;
      buf[0] = 0;
      if (!saved || p.getString(f.key, buf, f.max + 1) == 0) strlcpy(buf, saved ? "" : f.defS, f.max + 1);
    }
  }
  if (open) {
    s_rot = p.getUChar("rot", 1) & 3;
    uint8_t bl = p.getUChar("bl", 255);
    s_bl = bl < 10 ? 10 : bl;
    makeApPass(p);
    p.end();
  } else {
    strcpy(S->apPass, "cydpanel");   // NVS broken: still give a usable (fixed) hotspot password
  }
}

static const char *txt(int i) { return (S && i >= 0 && i < nF && F[i].kind != CFG_NUM) ? (const char *)F[i].ptr : ""; }
const char *cfgWifiSsid() { return txt(0); }
const char *cfgWifiPass() { return txt(1); }
const char *cfgHaHost() { return txt(2); }
uint16_t cfgHaPort() { return S ? S->port : HA_PORT; }
const char *cfgHaToken() { return txt(4); }

uint8_t cfgRotation() { return s_rot; }
uint8_t cfgBrightness() { return s_bl; }
static void saveByte(const char *key, uint8_t v) {
  Preferences p;
  if (p.begin(NS, false)) { p.putUChar(key, v); p.end(); }
}
void cfgSetRotation(uint8_t r) {
  r &= 3;
  if (r == s_rot) return;   // no flash write when nothing changed
  s_rot = r;
  saveByte("rot", r);
}
void cfgSetBrightness(uint8_t v) {
  if (v < 10) v = 10;
  if (v == s_bl) return;
  s_bl = v;
  saveByte("bl", v);
}

const char *cfgPlugName(int i) { return (S && i >= 0 && i < NPLUGS) ? S->plugName[i] : ""; }
const char *cfgTileName(int i) { return (S && i >= 0 && i < 4) ? S->tileName[i] : ""; }
int cfgTilePlug(int i) { return (S && i >= 0 && i < 4) ? S->tilePlug[i] : 0; }
int cfgAlertPlug() { return S ? S->alertPlug : 0; }
const char *cfgEntity(int e) { return (S && e >= 0 && e < E_COUNT) ? S->ent[e] : ""; }
const char *cfgSetupPass() { return S ? S->apPass : ""; }

// ---- setup-page fields -------------------------------------------------------------------------
int cfgFieldCount() { return S ? nF : 0; }
static bool isSecret(CfgKind k) { return k == CFG_WPASS || k == CFG_TOKEN; }
bool cfgFieldInfo(int i, CfgFieldInfo *o) {
  if (!S || i < 0 || i >= nF) return false;
  o->key = F[i].key; o->kind = F[i].kind; o->secret = isSecret(F[i].kind); o->max = F[i].max;
  return true;
}
int cfgFieldFind(const char *key) {
  for (int i = 0; S && i < nF; i++) if (!strcmp(F[i].key, key)) return i;
  return -1;
}
void cfgFieldValue(int i, char *out, size_t n) {
  if (!n) return;
  out[0] = 0;
  if (!S || i < 0 || i >= nF || isSecret(F[i].kind)) return;
  if (F[i].kind == CFG_NUM) snprintf(out, n, "%d", getNum(F[i]));
  else strlcpy(out, (const char *)F[i].ptr, n);
}
bool cfgFieldHasValue(int i) {
  if (!S || i < 0 || i >= nF) return false;
  return F[i].kind == CFG_NUM ? getNum(F[i]) != 0 : ((const char *)F[i].ptr)[0] != 0;
}

static bool isNumStr(const char *s) {
  if (!*s) return false;
  for (; *s; s++) if (*s < '0' || *s > '9') return false;
  return true;
}
// "domain.object_id", lowercase letters, digits and underscores only.
static bool isEntityId(const char *s) {
  const char *dot = strchr(s, '.');
  if (!dot || dot == s || !dot[1] || strchr(dot + 1, '.')) return false;
  for (const char *c = s; *c; c++)
    if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '.')) return false;
  return true;
}

const char *cfgFieldCheck(int i, const char *t) {
  if (!S || i < 0 || i >= nF || !t) return "unknown setting";
  const Field &f = F[i];
  size_t len = strlen(t);
  if (f.kind == CFG_NUM) {
    if (len > 5 || !isNumStr(t)) return "must be a whole number";
    long v = atol(t);
    if (v > f.max || (isPort(f) && v < 1)) return isPort(f) ? "must be 1-65535" : "must be 0-10";
    return nullptr;
  }
  if (len > f.max) return "is too long";
  for (const char *c = t; *c; c++)
    if ((uint8_t)*c < 0x20 || *c == 0x7f) return "has a character that isn't allowed";
  switch (f.kind) {
    case CFG_ENTITY:
      if (len && !isEntityId(t)) return "doesn't look like an entity ID (like sensor.kitchen_power)";
      break;
    case CFG_SWITCH:
      if (len && (!isEntityId(t) || strncmp(t, "switch.", 7))) return "must be a switch entity (starts with switch.)";
      break;
    case CFG_HOST:
      for (const char *c = t; *c; c++)
        if (!isalnum((uint8_t)*c) && *c != '.' && *c != '-') return "must be an IP address like 192.168.1.50";
      break;
    case CFG_WPASS: {
      if (len == 0) break;   // open network
      bool hex64 = len == 64;
      for (const char *c = t; hex64 && *c; c++) hex64 = isxdigit((uint8_t)*c);
      if (len < 8 || (len == 64 && !hex64)) return "must be 8-63 characters";
      break;
    }
    case CFG_TOKEN:
      for (const char *c = t; *c; c++)
        if ((uint8_t)*c <= 0x20 || (uint8_t)*c >= 0x7f) return "has a space or a character that isn't allowed";
      break;
    default: break;   // CFG_TEXT, CFG_SSID: any printable text
  }
  return nullptr;
}

bool cfgFieldSave(int i, const char *t) {
  if (cfgFieldCheck(i, t)) return false;
  Field &f = F[i];
  Preferences p;
  if (f.kind == CFG_NUM) {
    int v = clampNum(f, atol(t));
    if (v == getNum(f)) return true;   // unchanged: no flash write
    if (!p.begin(NS, false)) return false;
    bool ok = isPort(f) ? p.putUShort(f.key, v) : p.putUChar(f.key, v);
    p.end();
    return ok;   // RAM copy changes on the restart that follows saving
  }
  if (!strcmp(t, (const char *)f.ptr)) return true;
  if (!p.begin(NS, false)) return false;
  bool ok = p.putString(f.key, t) == strlen(t);
  p.end();
  if (ok && f.live) strlcpy((char *)f.ptr, t, f.max + 1);
  return ok;
}

void cfgResetToDefaults() {
  Preferences p;
  if (!p.begin(NS, false)) return;
  for (int i = 0; i < nF; i++) if (p.isKey(F[i].key)) p.remove(F[i].key);
  p.end();
}
