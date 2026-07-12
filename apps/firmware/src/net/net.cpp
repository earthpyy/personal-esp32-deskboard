#include "net.h"

#include <WiFi.h>
#include <secrets.h>

void net_init()
{
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

bool net_connected()
{
  return WiFi.status() == WL_CONNECTED;
}

const char *net_status_str()
{
  switch (WiFi.status())
  {
  case WL_CONNECTED:
    return "connected";
  case WL_NO_SSID_AVAIL:
    return "SSID not found";
  case WL_CONNECT_FAILED:
    return "connect failed";
  case WL_IDLE_STATUS:
  case WL_DISCONNECTED:
    return "connecting";
  default:
    return "unknown";
  }
}

const char *net_ip_str()
{
  static char buf[16];
  WiFi.localIP().toString().toCharArray(buf, sizeof(buf));
  return buf;
}

int net_rssi()
{
  return WiFi.RSSI();
}

const char *net_ssid()
{
  return WIFI_SSID;
}
