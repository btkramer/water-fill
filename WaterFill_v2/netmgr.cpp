#include "netmgr.h"
#include <WiFi.h>
#include <Preferences.h>
#include <esp_mac.h>
#include "platform.h"
#include "secrets.h"

namespace net {

static const char* HOSTNAME = "waterfill";
static const uint32_t AP_AFTER_MS = 45000;      // station down this long -> start the AP
static const uint32_t STA_RETRY_AP_MS = 180000; // re-test the saved network this often while the AP is up
static const uint32_t STA_RETRY_MS = 10000;     // ...and this often when there is no AP yet
static const uint32_t AP_GRACE_MS = 20000;      // keep the AP this long after the station reconnects

static Preferences pn;
static char g_ssid[33], g_pass[65], g_apPass[20], g_apName[24];
static volatile bool g_gotIp = false;
static bool g_apOn = false;
static uint32_t g_tLost = 0, g_tTry = 0, g_tConnected = 0, g_tRoam = 0, g_tApStop = 0;

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static bool g_pending = false;
static char p_ssid[33], p_pass[65], p_apPass[20];
static bool p_hasSsid, p_hasPass, p_hasAp;

static void startSta() {
  WiFi.disconnect();
  WiFi.begin(g_ssid, g_pass);
}

static void startAp() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  bool ok = WiFi.softAP(g_apName, g_apPass);
  g_apOn = ok;
  plat::log("Wi-Fi not connected: started access point \"%s\" (http://%s/)%s", g_apName,
            WiFi.softAPIP().toString().c_str(), ok ? "" : " FAILED");
}

static void stopAp() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  g_apOn = false;
  plat::log("Access point stopped (Wi-Fi connected)");
}

void begin() {
  pn.begin("net", false);
  String s = pn.getString("ssid", WIFI_SSID);
  String p = pn.getString("pass", WIFI_PASS);
  String a = pn.getString("appass", "waterfill");
  strlcpy(g_ssid, s.c_str(), sizeof g_ssid);
  strlcpy(g_pass, p.c_str(), sizeof g_pass);
  strlcpy(g_apPass, a.length() >= 8 ? a.c_str() : "waterfill", sizeof g_apPass);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(g_apName, sizeof g_apName, "WaterFill-%02X%02X", mac[4], mac[5]);
  WiFi.setHostname(HOSTNAME);  // DHCP name
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);  // join the strongest AP, not the first found
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { g_gotIp = true; }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  startSta();
  g_tTry = g_tLost = millis();
}

bool takeGotIp() { bool g = g_gotIp; g_gotIp = false; return g; }
bool connected() { return WiFi.status() == WL_CONNECTED; }
bool apActive() { return g_apOn; }
const char* apSsid() { return g_apName; }
const char* ssid() { return g_ssid; }
bool hasPassword() { return g_pass[0]; }
bool hasApPassword() { return g_apPass[0]; }

bool setCredentials(const char* s, const char* p, const char* a) {
  if (a && strlen(a) && (strlen(a) < 8 || strlen(a) > 19)) return false;
  if (s && strlen(s) > 32) return false;
  if (p && strlen(p) > 63) return false;
  portENTER_CRITICAL(&mux);
  p_hasSsid = s && *s;
  p_hasPass = p != nullptr;
  p_hasAp = a && *a;
  if (p_hasSsid) strlcpy(p_ssid, s, sizeof p_ssid);
  if (p_hasPass) strlcpy(p_pass, p, sizeof p_pass);
  if (p_hasAp) strlcpy(p_apPass, a, sizeof p_apPass);
  g_pending = true;
  portEXIT_CRITICAL(&mux);
  return true;
}

void service(uint32_t now) {
  if (g_pending) {
    portENTER_CRITICAL(&mux);
    bool hs = p_hasSsid, hp = p_hasPass, ha = p_hasAp;
    char s[33], p[65], a[20];
    memcpy(s, p_ssid, sizeof s); memcpy(p, p_pass, sizeof p); memcpy(a, p_apPass, sizeof a);
    g_pending = false;
    portEXIT_CRITICAL(&mux);
    if (hs) { strlcpy(g_ssid, s, sizeof g_ssid); pn.putString("ssid", g_ssid); }
    if (hp) { strlcpy(g_pass, p, sizeof g_pass); pn.putString("pass", g_pass); }
    if (ha) { strlcpy(g_apPass, a, sizeof g_apPass); pn.putString("appass", g_apPass); }
    plat::log("Wi-Fi settings changed: joining \"%s\"", g_ssid);
    startSta();
    g_tTry = g_tLost = now;
    if (ha && g_apOn) { WiFi.softAPdisconnect(true); g_apOn = false; }
  }

  if (connected()) {
    if (!g_tConnected) g_tConnected = now;
    g_tLost = 0;
    if (g_apOn) {
      if (!g_tApStop) g_tApStop = now;
      if (now - g_tApStop > AP_GRACE_MS && WiFi.softAPgetStationNum() == 0) { stopAp(); g_tApStop = 0; }
    }
    // Joined a distant access point? Re-scan and pick the strongest (a weak link made OTA fail).
    if (WiFi.RSSI() < -75 && now - g_tConnected > 60000 && now - g_tRoam > 600000) {
      g_tRoam = now;
      plat::log("Weak Wi-Fi (%d dBm): re-scanning", WiFi.RSSI());
      startSta();
    }
    return;
  }

  g_tConnected = 0;
  g_tApStop = 0;
  if (!g_tLost) g_tLost = now;
  if (!g_apOn && now - g_tLost > AP_AFTER_MS) startAp();

  uint32_t every = g_apOn ? STA_RETRY_AP_MS : STA_RETRY_MS;
  if (now - g_tTry >= every) {
    if (g_apOn && WiFi.softAPgetStationNum() > 0) {
      g_tTry = now;  // someone is using the AP: don't disturb them
      return;
    }
    g_tTry = now;
    plat::log("Wi-Fi retry: \"%s\"", g_ssid);
    startSta();
  }
}

void scanStart() {
  if (WiFi.scanComplete() == -1) return;  // already scanning
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false, false, 300);
}

int scanResult(char* json, size_t cap) {
  int n = WiFi.scanComplete();
  if (n < 0) return n == -1 ? -1 : -2;
  size_t o = 0;
  o += snprintf(json + o, cap - o, "[");
  int shown = 0;
  for (int i = 0; i < n && shown < 12; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (int j = 0; j < i; j++) if (WiFi.SSID(j) == s) dup = true;
    if (dup) continue;
    String e;
    for (size_t k = 0; k < s.length(); k++) { char c = s[k]; if (c == '"' || c == '\\') e += '\\'; if ((uint8_t)c >= 32) e += c; }
    o += snprintf(json + o, cap - o, "%s{\"s\":\"%s\",\"r\":%d,\"e\":%d}", shown ? "," : "", e.c_str(), WiFi.RSSI(i),
                  WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    if (o + 80 > cap) break;
    shown++;
  }
  snprintf(json + o, cap - o, "]");
  WiFi.scanDelete();
  return n;
}

}  // namespace net
