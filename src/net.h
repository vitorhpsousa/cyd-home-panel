#pragma once
// Wi-Fi: joins the home network from panel_settings.h and keeps rejoining if it drops.
#include <Arduino.h>

void netBegin();   // after cfgBegin()
void netLoop();    // call every loop()
