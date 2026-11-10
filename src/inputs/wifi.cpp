#include "wifi.h"

#include <WiFi.h>

#include "../secrets.h"

namespace {

void logEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("wifi: radio started");
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.printf("wifi: associated with %s, waiting for an address\n", kWifiSsid);
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("wifi: got address %s, signal %d dBm\n", WiFi.localIP().toString().c_str(),
                    WiFi.RSSI());
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      Serial.println("wifi: lost address");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.printf("wifi: disconnected, reason %u\n",
                    static_cast<unsigned>(info.wifi_sta_disconnected.reason));
      break;
    default:
      break;
  }
}

}  // namespace

void startWifi() {
  WiFi.onEvent(logEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(kWifiSsid, kWifiPassword);
  Serial.printf("wifi: joining %s\n", kWifiSsid);
}

bool wifiUp() {
  return WiFi.status() == WL_CONNECTED;
}

const char* wifiState() {
  switch (WiFi.status()) {
    case WL_IDLE_STATUS:
      return "idle";
    case WL_NO_SSID_AVAIL:
      return "network-not-found";
    case WL_SCAN_COMPLETED:
      return "scan-completed";
    case WL_CONNECTED:
      return "connected";
    case WL_CONNECT_FAILED:
      return "connect-failed";
    case WL_CONNECTION_LOST:
      return "connection-lost";
    case WL_DISCONNECTED:
      return "disconnected";
    default:
      return "no-shield";
  }
}
