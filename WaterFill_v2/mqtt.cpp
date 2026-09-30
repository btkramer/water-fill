#include "mqtt.h"
#include <WiFi.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "app.h"
#include "platform.h"

namespace mqtt {

static const char* BASE = "waterfill";
// No apostrophes: HA builds entity ids from these names (brian_dock_pro, not brian_s_dock_pro).
static const char* TANK_NAME[3] = {"CPAP", "Brian Dock Pro", "Dina Dock Pro"};
static const char* TANK_KEY[3] = {"cpap", "brian", "dina"};

static WiFiClient wifi;
static PubSubClient client(wifi);
static Preferences pm;
static Cfg cfg;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool g_connected = false, g_reconnect = false;
static char g_node[24];
static uint32_t lastAttempt = 0, lastPublish = 0, lastVersion = 0xFFFFFFFF;

static String topic(const char* leaf) { return String(BASE) + "/" + leaf; }

// ---------------------------------------------------------------- settings
static void loadCfg() {
  pm.begin("mqtt", false);
  cfg.en = pm.getBool("en", true);
  strlcpy(cfg.host, pm.getString("host", "192.168.1.5").c_str(), sizeof cfg.host);  // TrueNAS Mosquitto
  cfg.port = pm.getUShort("port", 1883);
  strlcpy(cfg.user, pm.getString("user", "").c_str(), sizeof cfg.user);
  strlcpy(cfg.pass, pm.getString("pass", "").c_str(), sizeof cfg.pass);
}

void getCfg(Cfg& c) { portENTER_CRITICAL(&mux); c = cfg; portEXIT_CRITICAL(&mux); }

bool setCfg(const Cfg& c, bool keepPass) {
  if (!c.host[0] || c.port == 0) return false;
  portENTER_CRITICAL(&mux);
  Cfg n = c;
  if (keepPass) strlcpy(n.pass, cfg.pass, sizeof n.pass);
  cfg = n;
  g_reconnect = true;
  portEXIT_CRITICAL(&mux);
  pm.putBool("en", n.en);
  pm.putString("host", n.host);
  pm.putUShort("port", n.port);
  pm.putString("user", n.user);
  pm.putString("pass", n.pass);
  return true;
}

bool connected() { return g_connected; }
const char* node() { return g_node; }

// ---------------------------------------------------------------- status JSON
static void humanState(const wf::Status& s, char* out, size_t n) {
  const char* t = s.target >= 0 ? TANK_NAME[s.target] : "";
  if (s.ioFault) snprintf(out, n, "I/O fault");
  else if (s.state == wf::RS_OZONE) snprintf(out, n, "Ozone running");
  else if (s.state == wf::RS_OPENING) snprintf(out, n, "Opening valve: %s", t);
  else if (s.state == wf::RS_FILLING) {
    if (s.waitSupply) snprintf(out, n, "Filling %s (waiting for supply)", t);
    else if (s.out.pump) snprintf(out, n, "Filling %s", t);
    else snprintf(out, n, "Filling %s (pump resting)", t);
  } else if (s.state == wf::RS_STOPPING) snprintf(out, n, "Stopping");
  else if (!s.autoMode) snprintf(out, n, "Manual - idle");
  else if (s.supplyEmpty) snprintf(out, n, "Waiting: supply empty");
  else if (s.outsideSchedule) snprintf(out, n, "Waiting: fill window closed");
  else snprintf(out, n, "Idle");
}

static void iso(uint32_t t, char* out, size_t n) {
  if (!t) { out[0] = 0; return; }
  time_t tt = t;
  struct tm g;
  gmtime_r(&tt, &g);
  snprintf(out, n, "%04d-%02d-%02dT%02d:%02d:%02d+00:00", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec);
}

static void publishStatus() {
  JsonDocument d;
  {
    Lock l;
    const wf::Status& s = app.status;
    char buf[64];
    humanState(s, buf, sizeof buf);
    d["state"] = buf;
    d["mode"] = s.autoMode ? "Auto" : "Manual";
    d["wait"] = s.waitSupply || s.supplyEmpty ? "Supply empty" : (s.outsideSchedule ? "Fill window closed" : (s.coolRemS ? "Pump resting" : ""));
    d["pump"] = s.out.pump;
    d["ozone"] = s.out.ozone;
    d["supplyEmpty"] = s.supplyEmpty;
    d["io"] = s.ioFault;
    uint32_t dayNow = app.timeValid ? plat::localDay() : 0;
    uint32_t pumpSec = 0, ozSec = 0;
    uint8_t tf[3] = {0, 0, 0};
    for (int i = app.hist.dayCount() - 1; i >= 0 && dayNow; i--) {
      const wf::Daily& dd = app.hist.dayAt(i);
      if (dd.day < dayNow) break;
      if (dd.day == dayNow) { pumpSec = dd.pumpSec; ozSec = dd.ozoneSec; memcpy(tf, dd.fills, 3); break; }
    }
    JsonArray tanks = d["tanks"].to<JsonArray>();
    for (int i = 0; i < 3; i++) {
      JsonObject t = tanks.add<JsonObject>();
      t["need"] = s.need[i];
      t["en"] = app.cfg.tankEnabled[i];
      t["fault"] = s.fault[i];
      t["today"] = tf[i];
      t["total"] = app.life.fills[i];
      iso(app.lastFill[i], buf, sizeof buf);
      t["last"] = buf;
    }
    JsonObject td = d["today"].to<JsonObject>();
    td["pumpMin"] = serialized(String(pumpSec / 60.0, 1));
    td["ozoneMin"] = serialized(String(ozSec / 60.0, 1));
    JsonObject life = d["life"].to<JsonObject>();
    life["pumpH"] = serialized(String(app.life.pumpSec / 3600.0, 2));
    life["ozoneH"] = serialized(String(app.life.ozoneSec / 3600.0, 2));
    life["cycles"] = app.life.pumpCycles;
    life["faults"] = app.life.faults;
    JsonObject set = d["set"].to<JsonObject>();
    set["pumpOn"] = app.cfg.pumpOnSec;
    set["pumpOff"] = app.cfg.pumpOffSec;
    set["ozoneRun"] = app.cfg.ozoneRunSec;
    set["manualMax"] = app.cfg.manualMaxSec;
  }
  d["rssi"] = WiFi.RSSI();
  d["up"] = millis() / 1000;
  d["fw"] = FW_VERSION;
  static char payload[1500];
  size_t n = serializeJson(d, payload, sizeof payload);
  if (n && n < sizeof payload) client.publish(topic("status").c_str(), (const uint8_t*)payload, n, false);
  lastPublish = millis();
}

// ---------------------------------------------------------------- commands
//   stop | ozone | ozstop | mode:auto | mode:manual | run:N | clear:N | tank:N:on|off | set:<key>:<value>
static void onMessage(char* t, byte* payload, unsigned int len) {
  if (String(t) != topic("cmd")) return;
  char msg[48];
  len = len < sizeof msg - 1 ? len : sizeof msg - 1;
  memcpy(msg, payload, len);
  msg[len] = 0;
  plat::log("MQTT command: %s", msg);
  if (!strcmp(msg, "stop")) queueCmd(CMD_STOP, 0);
  else if (!strcmp(msg, "ozone")) queueCmd(CMD_OZONE, 0);
  else if (!strcmp(msg, "ozstop")) queueCmd(CMD_OZONE_STOP, 0);
  else if (!strcmp(msg, "mode:auto")) queueCmd(CMD_AUTO, 0);
  else if (!strcmp(msg, "mode:manual")) queueCmd(CMD_MANUAL, 0);
  else if (!strncmp(msg, "run:", 4) && msg[4] >= '0' && msg[4] <= '2') queueCmd(CMD_RUN, msg[4] - '0');
  else if (!strncmp(msg, "clear:", 6) && msg[6] >= '0' && msg[6] <= '2') queueCmd(CMD_CLEAR, msg[6] - '0');
  else if (!strncmp(msg, "tank:", 5) && msg[5] >= '0' && msg[5] <= '2') {
    int i = msg[5] - '0';
    bool on = !strcmp(msg + 7, "on");
    changeSettings([&](wf::Settings& s) { s.tankEnabled[i] = on; });
  } else if (!strncmp(msg, "set:", 4)) {
    char* colon = strchr(msg + 4, ':');
    if (!colon) return;
    *colon = 0;
    int v = atoi(colon + 1);
    if (v < 0 || v > 65535) return;
    const char* k = msg + 4;
    changeSettings([&](wf::Settings& s) {
      if (!strcmp(k, "pumpOn")) s.pumpOnSec = v;
      else if (!strcmp(k, "pumpOff")) s.pumpOffSec = v;
      else if (!strcmp(k, "ozoneRun")) s.ozoneRunSec = v;
      else if (!strcmp(k, "manualMax")) s.manualMaxSec = v;
    });
  }
  lastVersion = 0xFFFFFFFE;  // publish the result promptly
}

// ---------------------------------------------------------------- Home Assistant discovery
static void disc(const char* component, const String& id, const String& name, void (*fill)(JsonDocument&, const void*), const void* arg) {
  JsonDocument d;
  d["name"] = name;
  d["uniq_id"] = String(g_node) + "_" + id;
  d["obj_id"] = String(g_node) + "_" + id;
  d["avty_t"] = topic("availability");
  fill(d, arg);
  JsonObject dev = d["dev"].to<JsonObject>();
  dev["ids"][0] = g_node;
  dev["name"] = "Water Fill";
  dev["mdl"] = "KC868-A6";
  dev["mf"] = "DIY";
  dev["sw"] = FW_VERSION;
  dev["cu"] = "http://waterfill.local/";  // name, not IP: survives a DHCP change
  String payload;
  serializeJson(d, payload);
  String t = String("homeassistant/") + component + "/" + g_node + "/" + id + "/config";
  client.publish(t.c_str(), (const uint8_t*)payload.c_str(), payload.length(), true);
}

struct Spec {                 // what a plain sensor / binary_sensor needs
  const char* tmpl;
  const char* unit;
  const char* devClass;
  const char* stateClass;
  const char* icon;
  const char* extra;          // "press" payload for buttons, etc.
};

static void fillSensor(JsonDocument& d, const void* a) {
  const Spec* s = (const Spec*)a;
  d["stat_t"] = topic("status");
  d["val_tpl"] = s->tmpl;
  if (s->unit) d["unit_of_meas"] = s->unit;
  if (s->devClass) d["dev_cla"] = s->devClass;
  if (s->stateClass) d["stat_cla"] = s->stateClass;
  if (s->icon) d["ic"] = s->icon;
}
static void fillBinary(JsonDocument& d, const void* a) {
  const Spec* s = (const Spec*)a;
  d["stat_t"] = topic("status");
  d["val_tpl"] = s->tmpl;
  d["pl_on"] = "True";
  d["pl_off"] = "False";
  if (s->devClass) d["dev_cla"] = s->devClass;
  if (s->icon) d["ic"] = s->icon;
}
static void fillButton(JsonDocument& d, const void* a) {
  const Spec* s = (const Spec*)a;
  d["cmd_t"] = topic("cmd");
  d["pl_prs"] = s->extra;
  if (s->icon) d["ic"] = s->icon;
}
static void fillSelect(JsonDocument& d, const void*) {
  d["stat_t"] = topic("status");
  d["val_tpl"] = "{{ value_json.mode }}";
  d["cmd_t"] = topic("cmd");
  d["cmd_tpl"] = "mode:{{ value | lower }}";
  d["options"][0] = "Auto";
  d["options"][1] = "Manual";
  d["ic"] = "mdi:cog-play";
}
static void fillSwitch(JsonDocument& d, const void* a) {
  const Spec* s = (const Spec*)a;   // tmpl = status template, extra = "tank:N:"
  d["stat_t"] = topic("status");
  d["val_tpl"] = s->tmpl;
  d["stat_on"] = "ON";
  d["stat_off"] = "OFF";
  d["cmd_t"] = topic("cmd");
  d["pl_on"] = String(s->extra) + "on";
  d["pl_off"] = String(s->extra) + "off";
  if (s->icon) d["ic"] = s->icon;
}
struct NumSpec { const char* key; const char* tmpl; int mn, mx, step; };
static void fillNumber(JsonDocument& d, const void* a) {
  const NumSpec* s = (const NumSpec*)a;
  d["stat_t"] = topic("status");
  d["val_tpl"] = s->tmpl;
  d["cmd_t"] = topic("cmd");
  d["cmd_tpl"] = String("set:") + s->key + ":{{ value | int }}";
  d["min"] = s->mn; d["max"] = s->mx; d["step"] = s->step;
  d["mode"] = "box";
  d["unit_of_meas"] = "s";
  d["dev_cla"] = "duration";
  d["ent_cat"] = "config";
}

static void publishDiscovery() {
  auto DS = [&](const char* comp, const String& id, const String& name, void (*f)(JsonDocument&, const void*), const Spec& sp) { disc(comp, id, name, f, &sp); };
  auto S = [&](const char* id, const char* name, const Spec& s) { disc("sensor", id, name, fillSensor, &s); };
  auto B = [&](const String& id, const String& name, const Spec& s) { disc("binary_sensor", id, name, fillBinary, &s); };

  S("state", "State", {"{{ value_json.state }}", nullptr, nullptr, nullptr, "mdi:water-pump", nullptr});
  S("wait", "Waiting reason", {"{{ value_json.wait }}", nullptr, nullptr, nullptr, "mdi:timer-sand", nullptr});
  S("pump_today", "Pump run today", {"{{ value_json.today.pumpMin }}", "min", "duration", "measurement", nullptr, nullptr});
  S("ozone_today", "Ozone run today", {"{{ value_json.today.ozoneMin }}", "min", "duration", "measurement", nullptr, nullptr});
  S("pump_hours", "Pump run hours", {"{{ value_json.life.pumpH }}", "h", "duration", "total_increasing", nullptr, nullptr});
  S("ozone_hours", "Ozone run hours", {"{{ value_json.life.ozoneH }}", "h", "duration", "total_increasing", nullptr, nullptr});
  S("pump_cycles", "Pump cycles", {"{{ value_json.life.cycles }}", nullptr, nullptr, "total_increasing", "mdi:counter", nullptr});
  S("faults_total", "Fill faults", {"{{ value_json.life.faults }}", nullptr, nullptr, "total_increasing", "mdi:alert-circle", nullptr});
  S("rssi", "Wi-Fi signal", {"{{ value_json.rssi }}", "dBm", "signal_strength", "measurement", nullptr, nullptr});

  B("pump_running", "Pump running", {"{{ value_json.pump }}", nullptr, "running", nullptr, nullptr, nullptr});
  B("ozone_running", "Ozone running", {"{{ value_json.ozone }}", nullptr, "running", nullptr, nullptr, nullptr});
  B("supply_empty", "Supply tank empty", {"{{ value_json.supplyEmpty }}", nullptr, "problem", nullptr, nullptr, nullptr});
  B("io_fault", "I/O fault", {"{{ value_json.io }}", nullptr, "problem", nullptr, nullptr, nullptr});

  char tmpl[5][96];
  for (int i = 0; i < 3; i++) {
    String k = TANK_KEY[i], n = TANK_NAME[i];
    snprintf(tmpl[0], sizeof tmpl[0], "{{ value_json.tanks[%d].need }}", i);
    B("needs_water_" + k, n + " needs water", {tmpl[0], nullptr, nullptr, nullptr, "mdi:water-alert", nullptr});
    snprintf(tmpl[1], sizeof tmpl[1], "{{ value_json.tanks[%d].fault }}", i);
    B("fault_" + k, n + " fill fault", {tmpl[1], nullptr, "problem", nullptr, nullptr, nullptr});
    snprintf(tmpl[2], sizeof tmpl[2], "{{ value_json.tanks[%d].today }}", i);
    DS("sensor", "fills_today_" + k, n + " fills today", fillSensor, Spec{tmpl[2], nullptr, nullptr, "measurement", "mdi:water-plus", nullptr});
    snprintf(tmpl[3], sizeof tmpl[3], "{{ value_json.tanks[%d].total }}", i);
    DS("sensor", "fills_total_" + k, n + " fills (total)", fillSensor, Spec{tmpl[3], nullptr, nullptr, "total_increasing", "mdi:counter", nullptr});
    snprintf(tmpl[4], sizeof tmpl[4], "{{ value_json.tanks[%d].last if value_json.tanks[%d].last else None }}", i, i);
    DS("sensor", "last_fill_" + k, n + " last fill", fillSensor, Spec{tmpl[4], nullptr, "timestamp", nullptr, nullptr, nullptr});

    char sw[96], pfx[16], run[16], clr[16];
    snprintf(sw, sizeof sw, "{{ 'ON' if value_json.tanks[%d].en else 'OFF' }}", i);
    snprintf(pfx, sizeof pfx, "tank:%d:", i);
    DS("switch", "autofill_" + k, n + " auto fill", fillSwitch, Spec{sw, nullptr, nullptr, nullptr, "mdi:water-check", pfx});
    snprintf(run, sizeof run, "run:%d", i);
    DS("button", "run_" + k, "Run pump: " + n, fillButton, Spec{nullptr, nullptr, nullptr, nullptr, "mdi:play", run});
    snprintf(clr, sizeof clr, "clear:%d", i);
    DS("button", "clear_" + k, "Clear fault: " + n, fillButton, Spec{nullptr, nullptr, nullptr, nullptr, "mdi:restore-alert", clr});
  }

  disc("select", "mode", "Mode", fillSelect, nullptr);
  DS("button", "stop", "Stop", fillButton, Spec{nullptr, nullptr, nullptr, nullptr, "mdi:stop", "stop"});
  DS("button", "ozone_run", "Run ozone", fillButton, Spec{nullptr, nullptr, nullptr, nullptr, "mdi:molecule", "ozone"});
  DS("button", "ozone_cancel", "Cancel ozone", fillButton, Spec{nullptr, nullptr, nullptr, nullptr, "mdi:cancel", "ozstop"});

  static const NumSpec nums[4] = {
    {"pumpOn", "{{ value_json.set.pumpOn }}", 1, 600, 1},
    {"pumpOff", "{{ value_json.set.pumpOff }}", 0, 600, 1},
    {"ozoneRun", "{{ value_json.set.ozoneRun }}", 1, 1800, 5},
    {"manualMax", "{{ value_json.set.manualMax }}", 10, 3600, 10}};
  static const char* numNames[4] = {"Pump max run", "Pump rest time", "Ozone run time", "Manual max run"};
  static const char* numIds[4] = {"set_pump_on", "set_pump_off", "set_ozone_run", "set_manual_max"};
  for (int i = 0; i < 4; i++) disc("number", numIds[i], numNames[i], fillNumber, &nums[i]);
}

// ---------------------------------------------------------------- connection / task
static void connectBroker() {
  lastAttempt = millis();
  Cfg c;
  getCfg(c);
  client.setServer(c.host, c.port);
  String will = topic("availability");
  bool ok = client.connect(g_node, c.user[0] ? c.user : nullptr, c.pass[0] ? c.pass : nullptr, will.c_str(), 0, true, "offline");
  if (!ok) {
    plat::log("MQTT: connect to %s:%u failed (rc=%d)", c.host, c.port, client.state());
    return;
  }
  plat::log("MQTT: connected to %s", c.host);
  client.publish(will.c_str(), "online", true);
  client.subscribe(topic("cmd").c_str());
  publishDiscovery();
  lastVersion = 0xFFFFFFFF;
}

static void task(void*) {
  for (;;) {
    Cfg c;
    getCfg(c);
    if (g_reconnect) { g_reconnect = false; if (client.connected()) client.disconnect(); lastAttempt = 0; }
    if (!c.en || WiFi.status() != WL_CONNECTED) {
      if (client.connected()) client.disconnect();
    } else if (!client.connected()) {
      if (millis() - lastAttempt > 10000 || lastAttempt == 0) connectBroker();
    } else {
      client.loop();
      uint32_t ver;
      { Lock l; ver = app.statusVersion; }
      if ((ver != lastVersion && millis() - lastPublish > 500) || millis() - lastPublish >= 5000) {
        lastVersion = ver;
        publishStatus();
      }
    }
    g_connected = client.connected();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void begin() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(g_node, sizeof g_node, "waterfill_%02x%02x%02x", mac[3], mac[4], mac[5]);
  loadCfg();
  client.setBufferSize(1536);
  client.setKeepAlive(30);
  client.setSocketTimeout(5);
  client.setCallback(onMessage);
  xTaskCreatePinnedToCore(task, "mqtt", 8192, nullptr, 1, nullptr, 0);
}

}  // namespace mqtt
