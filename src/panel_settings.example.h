#pragma once
// =====================================================================================
//  YOUR SETTINGS
//  1. Make a copy of this file in the same folder and call the copy  panel_settings.h
//  2. Fill in the copy. Only change the text between the "quotes" and the numbers.
//  panel_settings.h is never uploaded to GitHub (it is listed in .gitignore), so your
//  Wi-Fi password and Home Assistant token stay on your computer.
// =====================================================================================

// ---- Wi-Fi (the panel only works on 2.4 GHz Wi-Fi) ----------------------------------
#define WIFI_SSID "MyWiFiName"
#define WIFI_PASS "MyWiFiPassword"

// ---- Home Assistant -------------------------------------------------------------------
// HA_HOST is the IP address of your Home Assistant, e.g. "192.168.1.50"
// ("homeassistant.local" does NOT work here, use the numbers).
#define HA_HOST  "192.168.1.50"
#define HA_PORT  8123
// Home Assistant -> your profile (bottom left) -> Security -> Long-lived access tokens -> Create token
#define HA_TOKEN "paste-the-long-token-here"

// ---- Clock ----------------------------------------------------------------------------
// UK time with summer time. Other places: search "POSIX TZ string <your city>".
#define TZ_RULE "GMT0BST,M3.5.0/1,M10.5.0"
#define NIGHT_FROM 23        // screen dims from this hour...
#define NIGHT_TO   7         // ...until this hour. A touch wakes it for 30 s.

// ---- Electricity prices (GBP per kWh) ------------------------------------------------
// Below RATE_CHEAP shows green "Cheap", at or above RATE_PEAK shows red "Peak",
// anything between is amber "Standard". These suit Octopus Cosy.
#define RATE_CHEAP 0.18f
#define RATE_PEAK  0.35f
#define PEAK_MESSAGE "Peak rate now - until 19:00"   // shown (with a chime) when the peak starts

// ---- Home page ------------------------------------------------------------------------
// Where to find an entity ID: Home Assistant -> Settings -> Devices & services -> Entities,
// click the entity, then the cog icon. Copy the "Entity ID" exactly.
// Leave "" for anything you don't have.
#define ENT_INDOOR_TEMP  "sensor.indoor_temperature"
#define ENT_OUTDOOR_TEMP "sensor.outdoor_temperature"
#define ENT_WEATHER      "weather.forecast_home"     // the Met.no weather that HA installs by default
#define ENT_HEATING      "climate.hive"              // your thermostat
#define HIVE_BOOST 1     // 1 = holding the Heating card starts/stops a 30 min Hive boost. 0 = off (non-Hive thermostats)

// ---- Energy page (Octopus Energy integration by BottlecapDave) ---------------------
// Replace XXXX with the part of the name that is unique to your meter: find it by
// searching "current_rate" in the Entities list.
#define ENT_RATE       "sensor.octopus_energy_electricity_XXXX_current_rate"
#define ENT_NEXT_RATE  "sensor.octopus_energy_electricity_XXXX_next_rate"
#define ENT_TODAY_KWH  "sensor.octopus_energy_electricity_XXXX_current_accumulative_consumption"
#define ENT_TODAY_COST "sensor.octopus_energy_electricity_XXXX_current_accumulative_cost"
#define ENT_DEMAND     "sensor.octopus_energy_electricity_XXXX_current_demand"   // needs an Octopus Home Mini

// ---- Smart plugs (up to 10) -----------------------------------------------------------
// NAME  = what the screen shows (keep it short, about 13 letters max)
// POWER = the plug's power sensor in watts (usually ends in _power)
// COST  = optional Octopus "cost tracker" sensor for that plug, or ""
// Unused plugs: leave all three as "".
#define PLUG1_NAME  "Fridge"
#define PLUG1_POWER "sensor.fridge_plug_power"
#define PLUG1_COST  ""
#define PLUG2_NAME  "Kettle"
#define PLUG2_POWER "sensor.kettle_plug_power"
#define PLUG2_COST  ""
#define PLUG3_NAME  "Kitchen light"
#define PLUG3_POWER "sensor.kitchen_light_power"
#define PLUG3_COST  ""
#define PLUG4_NAME  "Lamp"
#define PLUG4_POWER "sensor.lamp_plug_power"
#define PLUG4_COST  ""
#define PLUG5_NAME  ""
#define PLUG5_POWER ""
#define PLUG5_COST  ""
#define PLUG6_NAME  ""
#define PLUG6_POWER ""
#define PLUG6_COST  ""
#define PLUG7_NAME  ""
#define PLUG7_POWER ""
#define PLUG7_COST  ""
#define PLUG8_NAME  ""
#define PLUG8_POWER ""
#define PLUG8_COST  ""
#define PLUG9_NAME  ""
#define PLUG9_POWER ""
#define PLUG9_COST  ""
#define PLUG10_NAME  ""
#define PLUG10_POWER ""
#define PLUG10_COST  ""

// ---- Controls page: up to 8 on/off switches ------------------------------------------
// SWITCH = the entity to turn on/off: a "switch.", "light." or "input_boolean." entity
// PLUG   = which plug number above shows its watts on the button (0 = none)
// The Controls page opens locked: tap "Unlock" at the bottom first (it locks again by itself).
// Optional: add  #define TILE1_CONFIRM_OFF 1  (any number 1-8) for something that must not be
// switched off by mistake, e.g. a desk with a computer: switching it OFF then needs a second tap.
// Only put things here that are safe to switch off by accident (not a fridge or freezer!).
#define TILE1_NAME   "Kitchen"
#define TILE1_SWITCH "switch.kitchen_light"
#define TILE1_PLUG   3
#define TILE2_NAME   "Lamp"
#define TILE2_SWITCH "switch.lamp_plug"
#define TILE2_PLUG   4
#define TILE3_NAME   ""
#define TILE3_SWITCH ""
#define TILE3_PLUG   0
#define TILE4_NAME   ""
#define TILE4_SWITCH ""
#define TILE4_PLUG   0
#define TILE5_NAME   ""
#define TILE5_SWITCH ""
#define TILE5_PLUG   0
#define TILE6_NAME   ""
#define TILE6_SWITCH ""
#define TILE6_PLUG   0
#define TILE7_NAME   ""
#define TILE7_SWITCH ""
#define TILE7_PLUG   0
#define TILE8_NAME   ""
#define TILE8_SWITCH ""
#define TILE8_PLUG   0

// ---- Optional "appliance stopped" alert -----------------------------------------------
// Red banner + chime when this switch is ON but the plug uses less than ALERT_WATTS for
// ALERT_MINUTES (for example a dehumidifier that stopped because its tank is full).
// The panel never switches this one, it only watches it. Leave ALERT_SWITCH "" to turn the alert off.
#define ALERT_SWITCH  ""
#define ALERT_PLUG    0     // which plug number above (1-10) measures it
#define ALERT_WATTS   50
#define ALERT_MINUTES 10
#define ALERT_TEXT    "Dehumidifier tank full?"

// ---- Optional Log page ----------------------------------------------------------------
// A fourth page (swipe past Controls) with buttons that note what is going on in the house,
// so energy and heating data can be explained later. It opens locked like Controls.
// It only appears when at least one of SHOWER_SCRIPT, FLOW_SELECT or LOG_BUTTONS is set.
//
// SHOWER_SCRIPT     = an HA script that pauses the heating while the hot water heats, e.g.
//                     "script.shower_pause" (see docs/setup-guide.md). The button shows
//                     "Heating paused" while the script runs; tapping it again runs SHOWER_END_SCRIPT.
// SHOWER_END_SCRIPT = an HA script that ends the pause early.
// SHOWER_MINUTES    = how long the script pauses for (only used for the "until" time shown).
// FLOW_SELECT       = an input_select that records a setting you change by hand (for example the
//                     boiler's flow temperature dial), and FLOW_OPTIONS the buttons to show for it
//                     (comma separated, each must be one of the input_select's options).
// LOG_BUTTONS       = one button per item, separated by ";". Each tap adds a line to HA's logbook
//                     under LOG_NAME, e.g. "Oven". A "/" starts the small second line on the
//                     button: "Sink/hot water" shows "Sink" over "hot water" and logs "Sink hot water".
#define SHOWER_SCRIPT     ""
#define SHOWER_END_SCRIPT ""
#define SHOWER_MINUTES    10
#define FLOW_SELECT       ""
#define FLOW_OPTIONS      "65,70,75"
#define LOG_BUTTONS       ""     // e.g. "1 hob;2 hobs;2 hobs/+ oven;Oven;Kitchen/other;Sink/hot water"
#define LOG_NAME          "House log"
