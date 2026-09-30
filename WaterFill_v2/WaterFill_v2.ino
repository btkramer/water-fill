// WaterFill v2 -- KC868-A6 water fill controller (CPAP / Brian's Dock Pro / Dina's Dock Pro + ozone).
// Logic: control.cpp (unit-tested on a PC: ../tests/run.sh). Web UI: ../web/index.html -> web_assets.h.
//   http://waterfill.local/   POST /api/update (firmware)   ArduinoOTA host "waterfill"
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include "secrets.h"
#include "app.h"
#include "platform.h"
#include "web.h"
#include "netmgr.h"
#include "mqtt.h"

App app;

static const char* HOSTNAME = "waterfill";
static const char* TANK_NAME[wf::N_TANKS] = {"CPAP", "Brian's Dock Pro", "Dina's Dock Pro"};

static bool servicesStarted = false;

// ---------------------------------------------------------------- events (called from the loop task, lock held)
static void onEvent(void*, uint8_t type, uint8_t arg, uint16_t a, uint16_t b, uint16_t pad) {
  wf::Event e{};
  e.t = plat::epoch();
  e.type = type; e.arg = arg; e.a = a; e.b = b; e.pad = pad;
  app.hist.addEvent(e);
  app.histDirty = true;
  if (e.t) {
    wf::Daily& d = app.hist.day(plat::localDay());
    if (type == wf::EV_FILL && arg < wf::N_TANKS) {
      if (d.fills[arg] < 255) d.fills[arg]++;
      app.lastFill[arg] = e.t;
    } else if (type == wf::EV_FILL_FAULT) {
      if (d.faults < 255) d.faults++;
    } else if (type == wf::EV_OZONE) {
      if (d.ozoneRuns < 255) d.ozoneRuns++;
      uint32_t s = d.ozoneSec + a;
      d.ozoneSec = s > 65535 ? 65535 : (uint16_t)s;
    }
  }
  if (type == wf::EV_FILL && arg < wf::N_TANKS) { app.life.fills[arg]++; app.lifeDirty = app.lifeUrgent = true; }
  else if (type == wf::EV_FILL_FAULT && arg < wf::N_TANKS) { app.life.faults++; app.life.faultMask |= 1u << arg; app.lifeDirty = app.lifeUrgent = true; }
  else if (type == wf::EV_CLEAR && arg < wf::N_TANKS) { app.life.faultMask &= ~(1u << arg); app.lifeDirty = app.lifeUrgent = true; }
  else if (type == wf::EV_OZONE) { app.lifeDirty = app.lifeUrgent = true; }
  if (type == wf::EV_FILL_FAULT || type == wf::EV_IOFAULT || type == wf::EV_BOOT) app.histUrgent = true;
  const char* tn = arg < wf::N_TANKS ? TANK_NAME[arg] : "?";
  switch (type) {
    case wf::EV_BOOT:       plat::log("Boot"); break;
    case wf::EV_FILL:       plat::log("%s fill ended: %us, pump %us, reason %u%s", tn, a, b, pad & 0xFF, (pad & 0x100) ? " (manual)" : ""); break;
    case wf::EV_FILL_FAULT: plat::log("FAULT %s: pump ran %us and it still never reported full", tn, a); break;
    case wf::EV_SUPPLY:     plat::log("Supply tank %s", arg ? "EMPTY" : "OK"); break;
    case wf::EV_OZONE:      plat::log("Ozone ran %us", a); break;
    case wf::EV_MODE:       plat::log("Mode: %s", arg ? "Auto" : "Manual"); break;
    case wf::EV_STOP:       plat::log("Stop pressed"); break;
    case wf::EV_CLEAR:      plat::log("Fault cleared: %s", tn); break;
    case wf::EV_SETTINGS:   plat::log("Settings changed"); break;
    case wf::EV_OTA:        plat::log("Firmware update"); break;
    case wf::EV_IOFAULT:    plat::log("I/O expander %s", arg ? "NOT RESPONDING" : "recovered"); break;
  }
}

// ---------------------------------------------------------------- pending requests from the web tasks (lock held)
static void applyPending() {
  for (uint8_t i = 0; i < app.qn; i++) {
    const Cmd& c = app.q[i];
    switch (c.type) {
      case CMD_AUTO:   app.ctl.cmdMode(true); break;
      case CMD_MANUAL: app.ctl.cmdMode(false); break;
      case CMD_STOP:   app.ctl.cmdStop(); break;
      case CMD_RUN:    app.ctl.cmdRun(c.arg); break;
      case CMD_OZONE:  app.ctl.cmdOzone(); break;
      case CMD_OZONE_STOP: app.ctl.cmdCancelOzone(); break;
      case CMD_CLEAR:  app.ctl.cmdClearFault(c.arg); break;
    }
  }
  app.qn = 0;
  if (app.cfgPending) {
    app.cfgPending = false;
    bool tzChanged = strcmp(app.cfgNew.tz, app.cfg.tz) != 0;
    app.cfgNew.autoMode = app.ctl.settings().autoMode;  // mode changes only through commands
    app.ctl.setSettings(app.cfgNew);
    if (tzChanged) plat::timeSetTz(app.cfgNew.tz);
  }
}

// ---------------------------------------------------------------- WiFi / services
static void startServices() {
  if (!servicesStarted) {
    web::begin();
    ArduinoOTA.setHostname(HOSTNAME);
    if (app.cfg.pin[0]) ArduinoOTA.setPassword(app.cfg.pin);
    ArduinoOTA.onStart([]() { app.otaBusy = true; plat::log("ArduinoOTA start"); });
    ArduinoOTA.onEnd([]() { app.rebootAt = millis(); app.rebootPending = true; });
    ArduinoOTA.onError([](ota_error_t) { app.otaBusy = false; });
    ArduinoOTA.begin();
    servicesStarted = true;
  }
  plat::timeStartNtp(app.cfg.tz);
  MDNS.end();
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
  plat::log("Online: %s  (%s.local)  RSSI %d", WiFi.localIP().toString().c_str(), HOSTNAME, WiFi.RSSI());
}

// ---------------------------------------------------------------- setup / loop
void setup() {
  plat::ioBegin();  // relays OFF before anything else: the expander keeps its state across a reset
  Serial.begin(115200);
  delay(100);
  app.lock = xSemaphoreCreateMutex();
  plat::log("WaterFill %s starting", FW_VERSION);

  plat::storageBegin();
  wf::Settings s;
  if (!plat::loadSettings(s)) { wf::settingsDefaults(s); plat::log("Using default settings"); }
  plat::timeSetTz(s.tz);
  plat::timeRestoreRtc();
  plat::loadHistory(app.hist);
  if (plat::loadLife(app.life)) plat::log("Lifetime: pump %.1f h, %lu fills", app.life.pumpSec / 3600.0,
      (unsigned long)(app.life.fills[0] + app.life.fills[1] + app.life.fills[2]));
  else {  // first run with lifetime counters: start from what the stored history already shows
    app.life = wf::Life{wf::LIFE_MAGIC};
    for (int i = 0; i < app.hist.dayCount(); i++) {
      const wf::Daily& d = app.hist.dayAt(i);
      app.life.pumpSec += d.pumpSec;
      app.life.ozoneSec += d.ozoneSec;
      app.life.faults += d.faults;
      for (int t = 0; t < wf::N_TANKS; t++) app.life.fills[t] += d.fills[t];
    }
    app.lifeDirty = app.lifeUrgent = true;
  }
  for (int i = 0; i < app.hist.eventCount(); i++) {
    const wf::Event& e = app.hist.event(i);
    if (e.type == wf::EV_FILL && e.arg < wf::N_TANKS) app.lastFill[e.arg] = e.t;
  }
  {
    Lock l;
    app.ctl.begin(millis(), s, onEvent, nullptr);
    app.cfg = app.ctl.settings();
    app.ctl.restoreFaults(app.life.faultMask);
    onEvent(nullptr, wf::EV_BOOT, (uint8_t)esp_reset_reason(), 0, 0, 0);
  }

  net::begin();
  mqtt::begin();

  esp_task_wdt_add(nullptr);  // a stalled control loop reboots the board; boot turns everything off first
}

void loop() {
  static uint32_t tCtl = 0, tSec = 0, tFlush = 0, tLife = 0, pumpMs = 0, lifeMs = 0, ozMs = 0;
  static bool prevPump = false;
  static bool tv = false;
  static uint16_t lm = 0;
  static bool otaLogged = false;
  uint32_t now = millis();
  esp_task_wdt_reset();

  if (net::takeGotIp()) startServices();

  if (now - tSec >= 1000) {
    tSec = now;
    tv = plat::timeValid();
    lm = tv ? plat::localMinutes() : 0;
    plat::timeService();
  }

  if (now - tCtl >= 50) {
    uint32_t dt = now - tCtl;
    tCtl = now;
    wf::Inputs in{};
    plat::ioPoll(in);
    in.timeValid = tv;
    in.localMinutes = lm;
    bool saveCfg = false;
    {
      Lock l;
      applyPending();
      if (app.otaBusy) {
        plat::ioAllOff();
        if (!otaLogged) { otaLogged = true; onEvent(nullptr, wf::EV_OTA, 0, 0, 0, 0); }
      } else {
        otaLogged = false;
        app.ctl.update(now, in);
        plat::ioWrite(app.ctl.outputs());
        const wf::Outputs& o = app.ctl.outputs();
        if (o.pump && !prevPump) { app.life.pumpCycles++; app.lifeDirty = true; }
        prevPump = o.pump;
        if (o.pump) { lifeMs += dt; while (lifeMs >= 1000) { lifeMs -= 1000; app.life.pumpSec++; app.lifeDirty = true; } }
        if (o.ozone) { ozMs += dt; while (ozMs >= 1000) { ozMs -= 1000; app.life.ozoneSec++; app.lifeDirty = true; } }
        if (app.ctl.outputs().pump && tv) {
          pumpMs += dt;
          if (pumpMs >= 1000) {
            wf::Daily& d = app.hist.day(plat::localDay());
            d.pumpSec += pumpMs / 1000;
            pumpMs %= 1000;
            app.histDirty = true;
          }
        }
      }
      // publish a snapshot for the web tasks
      const wf::Status& st = app.ctl.status();
      uint32_t sig = (st.out.pump ? 1u : 0) | (st.out.v1 ? 2u : 0) | (st.out.v2 ? 4u : 0) | (st.out.ozone ? 8u : 0) |
                     ((uint32_t)st.state << 4) | ((uint32_t)(st.target + 1) << 8) |
                     (st.need[0] ? 1u << 12 : 0) | (st.need[1] ? 1u << 13 : 0) | (st.need[2] ? 1u << 14 : 0) |
                     (st.fault[0] ? 1u << 15 : 0) | (st.fault[1] ? 1u << 16 : 0) | (st.fault[2] ? 1u << 17 : 0) |
                     (st.supplyEmpty ? 1u << 18 : 0) | (st.autoMode ? 1u << 19 : 0) | (st.ioFault ? 1u << 20 : 0) |
                     (st.waitSupply ? 1u << 21 : 0) | (st.outsideSchedule ? 1u << 22 : 0) | ((uint32_t)plat::rawDi() << 24);
      static uint32_t lastSig = 0;
      if (sig != lastSig) { lastSig = sig; app.statusVersion++; }
      app.status = st;
      app.cfg = app.ctl.settings();
      app.rawDi = plat::rawDi();
      app.rawDo = plat::rawDo();
      app.timeValid = tv;
      app.epoch = plat::epoch();
      saveCfg = app.ctl.takeSettingsDirty();
    }
    if (saveCfg) { Lock l; plat::saveSettings(app.cfg); }
  }

  // persist lifetime counters / latched faults: every 10 min, and right after a fill, ozone run or fault
  if (app.lifeDirty && (app.rebootPending || (app.lifeUrgent ? now - tLife > 2000 : now - tLife > 600000))) {
    Lock l;
    plat::saveLife(app.life);
    app.lifeDirty = app.lifeUrgent = false;
    tLife = now;
  }

  // persist history: every 5 min when changed, sooner (10 s) after faults / boot / before a reboot
  bool flushNow = app.rebootPending;
  if (!flushNow && app.histDirty && now - tFlush > (app.histUrgent ? 10000u : 300000u)) flushNow = true;
  if (flushNow) {
    Lock l;
    plat::saveHistory(app.hist);
    app.histDirty = app.histUrgent = false;
    tFlush = now;
  }
  if (app.rebootPending && (int32_t)(now - app.rebootAt) >= 0) { plat::ioAllOff(); ESP.restart(); }

  if (servicesStarted) { ArduinoOTA.handle(); web::tick(); }
  net::service(now);
  delay(2);
}
