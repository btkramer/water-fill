#include "web.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <WiFi.h>
#include "app.h"
#include "platform.h"
#include "web_assets.h"
#include "netmgr.h"
#include "mqtt.h"

namespace web {

static AsyncWebServer server(80);
static AsyncEventSource events("/stream");

// ------------------------------------------------------------------ JSON builders
static const char* stateName(wf::RunState s) {
  static const char* n[] = {"idle", "opening", "filling", "stopping", "ozone"};
  return n[s];
}

static const char* resetName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    default: return "other";
  }
}

// caller holds the lock
static void buildState(JsonDocument& d) {
  const wf::Status& s = app.status;
  d["t"] = app.epoch;
  d["up"] = millis() / 1000;
  d["tv"] = app.timeValid;
  d["mode"] = s.autoMode ? "auto" : "manual";
  d["st"] = stateName(s.state);
  d["tg"] = s.target;
  d["man"] = s.manualRun;
  JsonObject o = d["out"].to<JsonObject>();
  o["pump"] = s.out.pump; o["v1"] = s.out.v1; o["v2"] = s.out.v2; o["oz"] = s.out.ozone;
  d["cool"] = s.coolRemS;
  d["stint"] = s.stintS;
  JsonArray tanks = d["tanks"].to<JsonArray>();
  for (int i = 0; i < wf::N_TANKS; i++) {
    JsonObject t = tanks.add<JsonObject>();
    t["n"] = s.need[i];
    t["e"] = app.cfg.tankEnabled[i];
    t["f"] = s.fault[i];
    t["r"] = !(app.rawDi & (0x08 << i));  // electrical: input pulled LOW
    t["l"] = app.lastFill[i];
  }
  JsonObject sup = d["sup"].to<JsonObject>();
  sup["empty"] = s.supplyEmpty;
  sup["en"] = app.cfg.supplyEnabled;
  sup["r"] = !(app.rawDi & 0x04);
  d["btn"] = !(app.rawDi & 0x02);
  d["fillEl"] = s.fillElapsedS;
  d["fillPump"] = s.fillPumpS;
  d["fillLim"] = s.fillLimitS;
  d["manRem"] = s.manualRemS;
  d["ozRem"] = s.ozoneRemS;
  d["ozp"] = s.ozonePending;
  d["wait"] = s.waitSupply ? 1 : (s.outsideSchedule ? 2 : 0);
  d["io"] = s.ioFault;
  d["di"] = app.rawDi;
  d["do"] = app.rawDo;
  uint32_t dayNow = app.timeValid ? plat::localDay() : 0;
  JsonObject today = d["today"].to<JsonObject>();
  uint32_t pump = 0, oz = 0;
  uint8_t f[3] = {0, 0, 0};
  for (int i = app.hist.dayCount() - 1; i >= 0 && dayNow; i--) {
    const wf::Daily& dd = app.hist.dayAt(i);
    if (dd.day != dayNow) { if (dd.day < dayNow) break; else continue; }
    pump = dd.pumpSec; oz = dd.ozoneSec;
    memcpy(f, dd.fills, 3);
    break;
  }
  today["pump"] = pump; today["oz"] = oz;
  JsonArray tf = today["fills"].to<JsonArray>();
  for (int i = 0; i < 3; i++) tf.add(f[i]);
  JsonObject nt = d["net"].to<JsonObject>();
  nt["ap"] = net::apActive();
  nt["apSsid"] = net::apSsid();
  JsonObject mq = d["mqtt"].to<JsonObject>();
  mqtt::Cfg mc;
  mqtt::getCfg(mc);
  mq["en"] = mc.en;
  mq["ok"] = mqtt::connected();
  JsonObject lf = d["life"].to<JsonObject>();
  lf["pump"] = app.life.pumpSec; lf["oz"] = app.life.ozoneSec; lf["cycles"] = app.life.pumpCycles; lf["faults"] = app.life.faults;
  JsonArray lfl = lf["fills"].to<JsonArray>();
  for (int i = 0; i < 3; i++) lfl.add(app.life.fills[i]);
  JsonObject sys = d["sys"].to<JsonObject>();
  sys["rssi"] = WiFi.RSSI();
  sys["heap"] = ESP.getFreeHeap();
  sys["ip"] = WiFi.localIP().toString();
  sys["fw"] = FW_VERSION;
  sys["rst"] = resetName();
  sys["mac"] = WiFi.macAddress();
}

static void buildConfig(JsonDocument& d, const wf::Settings& c) {
  JsonArray t = d["tanks"].to<JsonArray>();
  for (int i = 0; i < wf::N_TANKS; i++) {
    JsonObject o = t.add<JsonObject>();
    o["en"] = c.tankEnabled[i]; o["inv"] = c.tankInvert[i]; o["max"] = c.tankMaxPumpSec[i];
  }
  JsonObject s = d["supply"].to<JsonObject>();
  s["en"] = c.supplyEnabled; s["inv"] = c.supplyInvert; s["trip"] = c.supplyTripS; s["recover"] = c.supplyRecoverS;
  JsonObject p = d["pump"].to<JsonObject>();
  p["on"] = c.pumpOnSec; p["off"] = c.pumpOffSec; p["lead"] = c.valveLeadMs; p["lag"] = c.valveLagMs;
  d["debounce"] = c.levelDebounceMs;
  JsonObject z = d["ozone"].to<JsonObject>();
  z["en"] = c.ozoneEnabled; z["run"] = c.ozoneRunSec;
  d["manualMax"] = c.manualMaxSec;
  JsonObject sc = d["sched"].to<JsonObject>();
  sc["en"] = c.schedEnabled; sc["start"] = c.schedStartMin; sc["end"] = c.schedEndMin;
  d["tz"] = c.tz;
  d["pinSet"] = c.pin[0] != 0;
  d["mode"] = c.autoMode ? "auto" : "manual";
}

static void setBool(JsonVariantConst v, bool& out) { if (v.is<bool>()) out = v.as<bool>(); }
static void setNum(JsonVariantConst v, uint16_t& out) {
  if (v.is<int>()) { int x = v.as<int>(); out = (uint16_t)(x < 0 ? 0 : (x > 65535 ? 65535 : x)); }
}

static void applyConfig(JsonVariantConst d, wf::Settings& c) {
  JsonArrayConst t = d["tanks"];
  for (int i = 0; i < wf::N_TANKS && i < (int)t.size(); i++) {
    setBool(t[i]["en"], c.tankEnabled[i]);
    setBool(t[i]["inv"], c.tankInvert[i]);
    setNum(t[i]["max"], c.tankMaxPumpSec[i]);
  }
  setBool(d["supply"]["en"], c.supplyEnabled);
  setBool(d["supply"]["inv"], c.supplyInvert);
  setNum(d["supply"]["trip"], c.supplyTripS);
  setNum(d["supply"]["recover"], c.supplyRecoverS);
  setNum(d["pump"]["on"], c.pumpOnSec);
  setNum(d["pump"]["off"], c.pumpOffSec);
  setNum(d["pump"]["lead"], c.valveLeadMs);
  setNum(d["pump"]["lag"], c.valveLagMs);
  setNum(d["debounce"], c.levelDebounceMs);
  setBool(d["ozone"]["en"], c.ozoneEnabled);
  setNum(d["ozone"]["run"], c.ozoneRunSec);
  setNum(d["manualMax"], c.manualMaxSec);
  setBool(d["sched"]["en"], c.schedEnabled);
  setNum(d["sched"]["start"], c.schedStartMin);
  setNum(d["sched"]["end"], c.schedEndMin);
  if (d["tz"].is<const char*>() && strlen(d["tz"].as<const char*>()) > 0) strlcpy(c.tz, d["tz"], sizeof c.tz);
  if (d["pin"].is<const char*>()) strlcpy(c.pin, d["pin"], sizeof c.pin);
}

// ------------------------------------------------------------------ request helpers
static bool authed(AsyncWebServerRequest* r) {
  char pin[12];
  { Lock l; strlcpy(pin, app.cfg.pin, sizeof pin); }
  if (!pin[0]) return true;
  const AsyncWebHeader* h = r->getHeader("X-Pin");
  if (h && h->value() == pin) return true;
  r->send(401, "application/json", "{\"err\":\"pin\"}");
  return false;
}

// Accumulates a small request body into request->_tempObject (freed by the library).
static void bodyCollector(AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) {
  if (total > 2048) return;
  if (index == 0) r->_tempObject = malloc(total + 1);
  char* b = (char*)r->_tempObject;
  if (!b) return;
  memcpy(b + index, data, len);
  b[index + len] = 0;
}

static bool readBody(AsyncWebServerRequest* r, JsonDocument& doc) {
  if (!r->_tempObject) { r->send(413, "application/json", "{\"err\":\"body\"}"); return false; }
  if (deserializeJson(doc, (const char*)r->_tempObject)) { r->send(400, "application/json", "{\"err\":\"json\"}"); return false; }
  return true;
}

static void postCmd(AsyncWebServerRequest* r) {
  if (!authed(r)) return;
  JsonDocument d;
  if (!readBody(r, d)) return;
  const char* c = d["c"] | "";
  int t = d["t"] | -1;
  if (!strcmp(c, "mode")) queueCmd(!strcmp(d["v"] | "", "auto") ? CMD_AUTO : CMD_MANUAL, 0);
  else if (!strcmp(c, "stop")) queueCmd(CMD_STOP, 0);
  else if (!strcmp(c, "ozone")) queueCmd(CMD_OZONE, 0);
  else if (!strcmp(c, "ozstop")) queueCmd(CMD_OZONE_STOP, 0);
  else if (!strcmp(c, "run") && t >= 0 && t < wf::N_TANKS) queueCmd(CMD_RUN, (int8_t)t);
  else if (!strcmp(c, "clear") && t >= 0 && t < wf::N_TANKS) queueCmd(CMD_CLEAR, (int8_t)t);
  else { r->send(400, "application/json", "{\"err\":\"cmd\"}"); return; }
  r->send(200, "application/json", "{\"ok\":true}");
}

static void postConfig(AsyncWebServerRequest* r) {
  if (!authed(r)) return;
  JsonDocument d;
  if (!readBody(r, d)) return;
  wf::Settings n;
  { Lock l; n = app.cfgPending ? app.cfgNew : app.cfg; }
  applyConfig(d.as<JsonVariantConst>(), n);
  wf::settingsClamp(n);
  { Lock l; app.cfgNew = n; app.cfgPending = true; }
  JsonDocument out;
  buildConfig(out, n);
  String s;
  serializeJson(out, s);
  r->send(200, "application/json", s);
}

static void getHistory(AsyncWebServerRequest* r) {
  AsyncResponseStream* res = r->beginResponseStream("application/json");
  Lock l;
  res->printf("{\"now\":%lu,\"day\":%u,\"ev\":[", (unsigned long)app.epoch, app.timeValid ? plat::localDay() : 0);
  for (int i = 0; i < app.hist.eventCount(); i++) {
    const wf::Event& e = app.hist.event(i);
    res->printf("%s[%lu,%u,%u,%u,%u,%u]", i ? "," : "", (unsigned long)e.t, e.type, e.arg, e.a, e.b, e.pad);
  }
  res->print("],\"days\":[");
  for (int i = 0; i < app.hist.dayCount(); i++) {
    const wf::Daily& d = app.hist.dayAt(i);
    res->printf("%s[%u,%lu,%u,%u,%u,%u,%u,%u]", i ? "," : "", d.day, (unsigned long)d.pumpSec,
                d.fills[0], d.fills[1], d.fills[2], d.ozoneRuns, d.ozoneSec, d.faults);
  }
  res->print("]}");
  r->send(res);
}

static void buildNet(JsonDocument& d) {
  d["ssid"] = net::ssid();
  d["hasPass"] = net::hasPassword();
  d["apSsid"] = net::apSsid();
  d["apPassSet"] = net::hasApPassword();
  d["ap"] = net::apActive();
  d["connected"] = net::connected();
  d["rssi"] = WiFi.RSSI();
  d["ip"] = WiFi.localIP().toString();
  mqtt::Cfg c;
  mqtt::getCfg(c);
  JsonObject m = d["mqtt"].to<JsonObject>();
  m["en"] = c.en; m["host"] = c.host; m["port"] = c.port; m["user"] = c.user;
  m["hasPass"] = c.pass[0] != 0; m["ok"] = mqtt::connected(); m["node"] = mqtt::node();
}

static void postNet(AsyncWebServerRequest* r) {
  if (!authed(r)) return;
  JsonDocument d;
  if (!readBody(r, d)) return;
  const char* ssid = d["ssid"].is<const char*>() ? d["ssid"].as<const char*>() : nullptr;
  const char* pass = d["pass"].is<const char*>() ? d["pass"].as<const char*>() : nullptr;
  const char* ap = d["apPass"].is<const char*>() ? d["apPass"].as<const char*>() : nullptr;
  if (ssid || pass || ap) {
    if (!net::setCredentials(ssid, pass, ap)) { r->send(400, "application/json", "{\"err\":\"wifi\"}"); return; }
  }
  if (d["mqtt"].is<JsonObject>()) {
    JsonObjectConst m = d["mqtt"];
    mqtt::Cfg c;
    mqtt::getCfg(c);
    if (m["en"].is<bool>()) c.en = m["en"];
    if (m["host"].is<const char*>()) strlcpy(c.host, m["host"], sizeof c.host);
    if (m["port"].is<int>()) c.port = (uint16_t)m["port"].as<int>();
    if (m["user"].is<const char*>()) strlcpy(c.user, m["user"], sizeof c.user);
    bool keep = !m["pass"].is<const char*>();
    if (!keep) strlcpy(c.pass, m["pass"], sizeof c.pass);
    if (!mqtt::setCfg(c, keep)) { r->send(400, "application/json", "{\"err\":\"mqtt\"}"); return; }
  }
  JsonDocument out;
  buildNet(out);
  String s;
  serializeJson(out, s);
  r->send(200, "application/json", s);
}

// ------------------------------------------------------------------ firmware update
static void onUpdateDone(AsyncWebServerRequest* r) {
  if (r->_tempObject) { r->send(401, "text/plain", "PIN required"); return; }  // flag set by onUpdateData
  bool ok = !Update.hasError();
  r->send(ok ? 200 : 500, "text/plain", ok ? "OK, rebooting" : "Update failed");
  if (ok) { app.rebootAt = millis() + 600; app.rebootPending = true; }
  else app.otaBusy = false;
}

static void onUpdateData(AsyncWebServerRequest* r, String filename, size_t index, uint8_t* data, size_t len, bool final) {
  if (index == 0) {
    char pin[12];
    { Lock l; strlcpy(pin, app.cfg.pin, sizeof pin); }
    const AsyncWebHeader* h = r->getHeader("X-Pin");
    if (pin[0] && !(h && h->value() == pin)) { r->_tempObject = malloc(1); return; }
    plat::log("HTTP OTA start: %s", filename.c_str());
    app.otaBusy = true;  // the loop forces all outputs off while we write flash
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { Update.printError(Serial); app.otaBusy = false; }
  }
  if (r->_tempObject) return;  // denied
  if (len && !Update.hasError() && Update.write(data, len) != len) Update.printError(Serial);
  delay(1);  // yield: a long flash write on the AsyncTCP task trips the watchdog (../OTA_HTTP.md)
  if (final) {
    if (Update.end(true)) plat::log("HTTP OTA done: %u bytes", (unsigned)(index + len));
    else Update.printError(Serial);
  }
}

// ------------------------------------------------------------------ public
void begin() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    AsyncWebServerResponse* res = r->beginResponse(200, "text/html", WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
    res->addHeader("Content-Encoding", "gzip");
    res->addHeader("Cache-Control", "no-cache");
    r->send(res);
  });
  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    { Lock l; buildState(d); }
    String s;
    serializeJson(d, s);
    r->send(200, "application/json", s);
  });
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    { Lock l; buildConfig(d, app.cfg); }
    String s;
    serializeJson(d, s);
    r->send(200, "application/json", s);
  });
  server.on("/api/config", HTTP_POST, postConfig, nullptr, bodyCollector);
  server.on("/api/cmd", HTTP_POST, postCmd, nullptr, bodyCollector);
  server.on("/api/history", HTTP_GET, getHistory);
  server.on("/api/net", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    buildNet(d);
    String s;
    serializeJson(d, s);
    r->send(200, "application/json", s);
  });
  server.on("/api/net", HTTP_POST, postNet, nullptr, bodyCollector);
  server.on("/api/net/scan", HTTP_GET, [](AsyncWebServerRequest* r) {
    char* buf = (char*)malloc(1800);
    if (!buf) { r->send(500); return; }
    int n = net::scanResult(buf, 1800);
    if (n >= 0) {
      String out = String("{\"done\":true,\"nets\":") + buf + "}";
      r->send(200, "application/json", out);
    } else {
      if (n == -2) net::scanStart();
      r->send(200, "application/json", "{\"done\":false}");
    }
    free(buf);
  });
  server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest* r) {
    char* buf = (char*)malloc(4600);
    if (!buf) { r->send(500); return; }
    plat::logDump(buf, 4600);
    r->send(200, "text/plain", buf);
    free(buf);
  });
  server.on("/api/update", HTTP_POST, onUpdateDone, onUpdateData);
  events.onConnect([](AsyncEventSourceClient* c) { c->send("hi", "hello", millis(), 2000); });
  server.addHandler(&events);
  server.onNotFound([](AsyncWebServerRequest* r) { r->send(404, "text/plain", "Not found"); });
  server.begin();
}

void tick() {
  static uint32_t lastSend = 0, lastVer = 0;
  if (events.count() == 0) return;
  uint32_t now = millis();
  bool changed;
  { Lock l; changed = app.statusVersion != lastVer; }
  if (now - lastSend < (changed ? 250u : 1000u)) return;
  JsonDocument d;
  { Lock l; lastVer = app.statusVersion; buildState(d); }
  char buf[1600];
  size_t n = serializeJson(d, buf, sizeof buf);
  if (n == 0 || n >= sizeof buf) return;
  events.send(buf, "state", now);
  lastSend = now;
}

}  // namespace web
