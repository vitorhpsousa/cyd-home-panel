#pragma once
// Wi-Fi: joins the home network from config.h, and runs the setup hotspot + web page.
#include <Arduino.h>

void netBegin();                 // after cfgBegin(): join Wi-Fi (or start the setup hotspot if none is set)
void netLoop();                  // call every loop()
bool netSetupActive();           // setup hotspot running
void netStartSetup();            // start the hotspot + setup page (hidden menu)
void netStopSetup();
// Up to 3 short lines telling the user how to reach the setup page, e.g. hotspot name, password, address.
// Returns the number of lines written (0 if setup is not running).
int netSetupLines(char lines[3][48]);
