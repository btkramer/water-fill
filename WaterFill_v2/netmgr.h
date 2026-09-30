// Wi-Fi station + fallback access point + editable credentials (stored in NVS).
//
// If the station can't connect for 45 s the controller also starts its own access point
// ("WaterFill-XXXX") serving the same web UI at http://192.168.4.1/ so the Wi-Fi settings can be
// fixed from a phone. While the AP is up it re-tries the saved network every 3 minutes (skipped
// while someone is connected to the AP, so the settings page isn't interrupted). Once the
// station connects, the AP shuts down. The fill controller never depends on any of this.
#pragma once
#include <Arduino.h>

namespace net {

void begin();
void service(uint32_t now);          // call from loop()
bool takeGotIp();                    // true once per (re)connection
bool connected();
bool apActive();
const char* apSsid();
const char* ssid();
bool hasPassword();
bool hasApPassword();

// Request new credentials (any task). nullptr = keep the current value.
// AP password must be 8..19 characters.
bool setCredentials(const char* ssid, const char* pass, const char* apPass);

// Asynchronous network scan (for the settings page): call scanStart(), poll scanResult().
void scanStart();
int scanResult(char* json, size_t cap);   // -2 idle, -1 scanning, >=0 number found (json filled)

}  // namespace net
