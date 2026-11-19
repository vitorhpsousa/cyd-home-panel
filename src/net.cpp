// STUB: joins Wi-Fi only. The setup-portal work adds the hotspot and the setup page.
#include "net.h"
#include "config.h"
#include <WiFi.h>

void netBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("cyd-home-panel");
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfgWifiSsid(), cfgWifiPass());
}
void netLoop() {}
bool netSetupActive() { return false; }
void netStartSetup() {}
void netStopSetup() {}
int netSetupLines(char lines[3][48]) { (void)lines; return 0; }
