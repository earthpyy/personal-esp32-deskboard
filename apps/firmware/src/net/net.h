#pragma once

void net_init();
bool net_connected();
const char *net_status_str();
const char *net_ip_str();
int net_rssi();
const char *net_ssid();
