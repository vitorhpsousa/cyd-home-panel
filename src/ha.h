#pragma once
// Home Assistant REST client. A background task polls each entity with GET /api/states/<id>
// (fast group every 5 s, slow group every 30 s) and runs queued service calls.
// REST, not the websocket API: the Octopus cost sensors carry 3-4 KB attribute lists that change
// every half hour; a filtered streaming parse of one entity at a time keeps RAM use tiny.
#include <Arduino.h>
#include <time.h>

enum EntId {
  // slow (every 30 s)
  E_TIN, E_TOUT, E_CLIMATE, E_WEATHER, E_RATE, E_NEXT, E_KWH, E_COST,
  C_PLUG1, C_PLUG10 = C_PLUG1 + 9,           // optional cost trackers, one per plug
  // fast (every 5 s)
  E_DEMAND,
  P_PLUG1, P_PLUG10 = P_PLUG1 + 9,           // plug power
  S_TILE1, S_TILE8 = S_TILE1 + 7,            // the Controls-page switches (the only ones the panel may toggle)
  S_ALERT,                                   // watched only, never switched
  E_FLOW,                                    // Log page: input_select that records the boiler flow setting
  S_SHOWER,                                  // Log page: the shower-pause script (state "on" while it runs)
  X_LOG,                                     // not an entity: lets logbook lines use the command slots
  E_COUNT
};
#define E_FIRST_FAST E_DEMAND
#define NPLUGS 10
#define NTILES 8

struct EntVal {
  bool ok;              // got at least one good reading
  bool avail;           // state is not unavailable/unknown
  char s[24];           // raw state
  float v;              // state as number (NAN if not numeric)
  float temp, curTemp;  // climate: temperature (target), current_temperature
  char action[12];      // climate hvac_action
  char preset[10];      // climate preset_mode
  time_t end;           // octopus rate: end of the period (UTC), 0 if none
  time_t since;         // script: last_triggered (UTC), 0 if none
};

void haBegin();                          // starts the poll task (call after WiFi.begin)
void haSnapshot(EntVal out[E_COUNT]);    // thread-safe copy of all values
bool haOnline();                         // last request succeeded and was < 60 s ago
const char *haEntityId(int e);
// Service calls (queued; the task runs them and then re-reads the entity).
bool haSet(int switchEnt, bool on);   // only the Controls-page switches; false = not sent (offline/busy)
void haBoost(float temperature, int minutes);
void haBoostOff();
// Log page (only these: never a switch, never another entity). false = not sent (offline/busy).
bool haShower(bool start);            // start = run SHOWER_SCRIPT, else run SHOWER_END_SCRIPT
bool haFlow(const char *option);      // select one of FLOW_OPTIONS on FLOW_SELECT
bool haLog(const char *message);      // logbook.log under LOG_NAME
