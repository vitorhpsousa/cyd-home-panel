#include "net.h"
#include "config.h"
#include <WiFi.h>

static uint32_t downSince = 0, lastTry = 0;

void netBegin() {
  WiFi.persistent(false);   // settings live in panel_settings.h; don't let the driver write flash
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("cyd-home-panel");
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfgWifiSsid(), cfgWifiPass());
  lastTry = millis();
}

// The driver's auto-reconnect gives up after some disconnect reasons, so retry by hand:
// after 20 s down, at most once a minute.
void netLoop() {
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) { downSince = 0; return; }
  if (!downSince) downSince = now;
  if (now - downSince > 20000 && now - lastTry > 60000) {
    lastTry = now;
    WiFi.disconnect(false);
    WiFi.begin(cfgWifiSsid(), cfgWifiPass());
  }
}
