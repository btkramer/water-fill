// Host-side tests for the controller. Build/run: tests/run.sh
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../WaterFill_v2/control.h"
#include "../WaterFill_v2/history.h"

using namespace wf;

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL %s:%d  %s   (t=%u)\n", __FILE__, __LINE__, #c, (unsigned)sim_now()); } } while (0)

struct Ev { uint8_t type, arg; uint16_t a, b, pad; };

struct Sim {
  Controller c;
  Settings s;
  Inputs in{};
  uint32_t now = 100000;  // start away from 0 to shake out wraparound-style bugs
  std::vector<Ev> evs;
  // invariant tracking
  Outputs prev{};
  uint32_t lastValveChange = 0, lastPumpOff = 0, pumpOffFor = 0, runAcc = 0;
  bool invariants = true;

  static void sink(void* ctx, uint8_t t, uint8_t arg, uint16_t a, uint16_t b, uint16_t pad) {
    ((Sim*)ctx)->evs.push_back({t, arg, a, b, pad});
  }
  Sim() { settingsDefaults(s); }
  void begin() { c.begin(now, s, &Sim::sink, this); lastValveChange = lastPumpOff = now; }
  uint32_t nowMs() const { return now; }

  void tick() {
    now += 50;
    c.update(now, in);
    if (invariants) check();
  }
  void run(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 50) tick(); }

  void check();
  int count(uint8_t type) const { int n = 0; for (auto& e : evs) if (e.type == type) n++; return n; }
};

static Sim* g_sim;
static uint32_t sim_now() { return g_sim ? g_sim->now : 0; }

void Sim::check() {
  const Outputs& o = c.outputs();
  const Status& st = c.status();
  const Settings& cfg = c.settings();
  CHECK(!(o.v1 && o.v2));                                   // never both valves
  if (o.pump) {
    CHECK(st.state == RS_FILLING);                          // pump only while filling
    CHECK(st.target >= 0);
    CHECK(o.v1 == (st.target == 0) && o.v2 == (st.target == 1));  // route matches the target tank
    CHECK(!st.supplyEmpty);                                 // never pumps an empty supply
    CHECK(!st.ioFault);
    CHECK(!o.ozone);
  }
  if (o.ozone) CHECK(st.state == RS_OZONE && !o.pump && !o.v1 && !o.v2);
  // valves move only with the pump stopped, lag after pump-off, lead before pump-on
  if (o.v1 != prev.v1 || o.v2 != prev.v2) {
    CHECK(!o.pump && !prev.pump);
    CHECK(now - lastPumpOff >= cfg.valveLagMs || lastPumpOff == lastValveChange);
    lastValveChange = now;
  }
  if (!o.pump && prev.pump) lastPumpOff = now;
  if (o.pump && !prev.pump) CHECK(now - lastValveChange >= cfg.valveLeadMs);
  // continuous run limit (a rest of pumpOffSec resets it)
  if (cfg.pumpOffSec > 0) {
    if (o.pump) { runAcc += 50; pumpOffFor = 0; }
    else { pumpOffFor += 50; if (pumpOffFor >= cfg.pumpOffSec * 1000u - 100) runAcc = 0; }
    CHECK(runAcc <= cfg.pumpOnSec * 1000u + 150);
  }
  prev = o;
}

static void needs(Sim& s, bool c0, bool c1, bool c2) { s.in.tankActive[0] = c0; s.in.tankActive[1] = c1; s.in.tankActive[2] = c2; }

static void test_basic_fill() {
  printf("basic fill\n");
  Sim s; g_sim = &s; s.begin();
  s.run(1000);
  needs(s, true, false, false);
  s.run(1500);  // level debounce is 2 s
  CHECK(s.c.status().state == RS_IDLE);
  s.run(1500);
  CHECK(s.c.status().state == RS_OPENING);
  CHECK(s.c.outputs().v1 && !s.c.outputs().pump);          // valve first, pump later
  s.run(1200);
  CHECK(s.c.outputs().pump && s.c.outputs().v1);
  needs(s, false, false, false);
  s.run(2200);                                             // debounce then stop
  CHECK(!s.c.outputs().pump);
  CHECK(s.c.outputs().v1);                                 // valve holds for the lag period
  s.run(1200);
  CHECK(!s.c.outputs().v1 && s.c.status().state == RS_IDLE);
  CHECK(s.count(EV_FILL) == 1);
}

static void test_priority_and_routes() {
  printf("priority and routes\n");
  Sim s; g_sim = &s; s.s.pumpOnSec = 600; s.begin();
  needs(s, true, true, true);
  s.run(8000);
  CHECK(s.c.status().target == 0 && s.c.outputs().v1 && !s.c.outputs().v2);
  needs(s, false, true, true);
  s.run(6000);
  CHECK(s.c.status().target == 1 && !s.c.outputs().v1 && s.c.outputs().v2 && s.c.outputs().pump);
  needs(s, false, false, true);
  s.run(6000);
  CHECK(s.c.status().target == 2 && !s.c.outputs().v1 && !s.c.outputs().v2 && s.c.outputs().pump);
  needs(s, false, false, false);
  s.run(6000);
  CHECK(s.c.status().state == RS_IDLE && !s.c.outputs().pump);
  CHECK(s.count(EV_FILL) == 3);
}

static void test_disabled_tank() {
  printf("disabled tank\n");
  Sim s; g_sim = &s; s.s.tankEnabled[0] = false; s.begin();
  needs(s, true, false, false);
  s.run(10000);
  CHECK(s.c.status().state == RS_IDLE && !s.c.outputs().v1);
  s.s.tankEnabled[0] = true; s.c.setSettings(s.s);
  s.run(6000);
  CHECK(s.c.status().target == 0);
  s.s.tankEnabled[0] = false; s.c.setSettings(s.s);       // disabling mid-fill stops it
  s.run(3000);
  CHECK(s.c.status().state == RS_IDLE && !s.c.outputs().pump);
}

static void test_pump_governor() {
  printf("pump governor\n");
  Sim s; g_sim = &s; s.s.pumpOnSec = 10; s.s.pumpOffSec = 10; s.begin();
  needs(s, false, false, true);
  s.run(6000);
  int on = 0, off = 0;
  for (int i = 0; i < 60000 / 50; i++) { s.tick(); if (s.c.outputs().pump) on++; else off++; }
  double duty = on / double(on + off);
  CHECK(duty > 0.40 && duty < 0.60);                       // about 10 on / 10 off
}

static void test_supply_interlock() {
  printf("supply interlock\n");
  Sim s; g_sim = &s; s.begin();
  s.in.supplyActive = true;                                // supply empty (input active)
  needs(s, true, false, false);
  s.run(12000);
  CHECK(s.c.status().supplyEmpty && s.c.status().state == RS_IDLE && !s.c.outputs().pump);
  s.in.supplyActive = false;
  s.run(9000);
  CHECK(s.c.status().supplyEmpty && s.c.status().state == RS_IDLE);   // recovery delay is 10 s
  s.run(3000);
  CHECK(!s.c.status().supplyEmpty && s.c.status().state != RS_IDLE);
  s.run(3000);
  CHECK(s.c.outputs().pump);
  s.in.supplyActive = true;                                // runs dry mid-fill: pump stops, valves stay
  s.run(7000);
  CHECK(!s.c.outputs().pump && s.c.status().waitSupply && s.c.outputs().v1);
  s.in.supplyActive = false;
  s.run(12000);
  CHECK(s.c.outputs().pump || s.c.status().coolRemS > 0);
  s.s.supplyEnabled = false; s.in.supplyActive = true; s.c.setSettings(s.s);   // interlock off
  s.run(8000);
  CHECK(!s.c.status().supplyEmpty);
}

static void test_fault_latch() {
  printf("fill fault\n");
  Sim s; g_sim = &s; s.s.tankMaxPumpSec[1] = 15; s.s.pumpOnSec = 5; s.s.pumpOffSec = 1; s.begin();
  needs(s, false, true, false);
  s.run(60000);                                            // never reports full
  CHECK(s.c.status().fault[1]);
  CHECK(s.count(EV_FILL_FAULT) == 1);
  CHECK(s.c.status().state == RS_IDLE && !s.c.outputs().pump);
  s.run(20000);
  CHECK(s.c.status().state == RS_IDLE);                    // latched: does not retry
  s.c.cmdClearFault(1);
  s.run(9000);
  CHECK(s.c.status().target == 1);                         // retries after the operator clears it
}

static void test_mode_and_stop() {
  printf("mode change and stop\n");
  Sim s; g_sim = &s; s.s.pumpOnSec = 600; s.begin();
  needs(s, true, false, false);
  s.run(9000);
  CHECK(s.c.outputs().pump);
  s.c.cmdMode(false);                                      // Auto -> Manual stops the fill
  s.run(300);
  CHECK(!s.c.outputs().pump);
  s.run(2000);
  CHECK(s.c.status().state == RS_IDLE && !s.c.outputs().v1);
  s.run(10000);
  CHECK(s.c.status().state == RS_IDLE);                    // manual mode: never starts by itself
  s.c.cmdMode(true);
  s.run(9000);
  CHECK(s.c.outputs().pump);
  s.c.cmdStop();                                           // Stop: everything off, Manual
  s.run(300);
  CHECK(!s.c.outputs().pump && !s.c.status().autoMode);
  s.run(12000);
  CHECK(s.c.status().state == RS_IDLE);
}

static void test_manual() {
  printf("manual run\n");
  Sim s; g_sim = &s; s.s.autoMode = false; s.s.manualMaxSec = 20; s.s.pumpOnSec = 600; s.begin();
  s.run(4000);
  CHECK(!s.c.cmdRun(0) == false);                          // accepted in manual
  s.run(3000);
  CHECK(s.c.outputs().pump && s.c.outputs().v1);          // level switches are ignored in manual
  s.c.cmdRun(1);                                           // change tank: pump stops before valves move
  s.run(5000);
  CHECK(s.c.outputs().pump && s.c.outputs().v2 && !s.c.outputs().v1);
  s.run(25000);                                            // timer stops it
  CHECK(!s.c.outputs().pump && s.c.status().state == RS_IDLE);
  bool sawManualTimeout = false;
  for (auto& e : s.evs) if (e.type == EV_FILL && (e.pad & 0xFF) == SR_MANUAL_TIMEOUT) sawManualTimeout = true;
  CHECK(sawManualTimeout);
  Sim a; g_sim = &a; a.begin();                            // Auto mode refuses manual runs
  CHECK(!a.c.cmdRun(0));
}

static void test_ozone() {
  printf("ozone\n");
  Sim s; g_sim = &s; s.s.ozoneRunSec = 20; s.begin();
  s.run(4000);
  CHECK(s.c.cmdOzone());
  s.run(500);
  CHECK(s.c.outputs().ozone && !s.c.outputs().pump);
  s.run(21000);
  CHECK(!s.c.outputs().ozone && s.c.status().state == RS_IDLE);
  CHECK(s.count(EV_OZONE) == 1);
  s.in.ozoneButton = true; s.run(300); s.in.ozoneButton = false;   // physical button
  s.run(500);
  CHECK(s.c.outputs().ozone);
  s.c.cmdStop();
  s.run(200);
  CHECK(!s.c.outputs().ozone);
  Sim m; g_sim = &m; m.s.autoMode = false; m.begin();   // cancelling ozone keeps the mode
  m.run(4000); m.c.cmdOzone(); m.run(1000);
  CHECK(m.c.outputs().ozone);
  m.c.cmdCancelOzone(); m.run(1500);
  CHECK(!m.c.outputs().ozone && !m.c.status().autoMode && m.c.status().state == RS_IDLE);
  Sim a; g_sim = &a; a.begin();
  a.run(4000); a.c.cmdOzone(); a.run(1000);
  a.c.cmdCancelOzone(); a.run(1500);
  CHECK(!a.c.outputs().ozone && a.c.status().autoMode);
}

static void test_schedule() {
  printf("schedule window\n");
  Sim s; g_sim = &s; s.s.schedEnabled = true; s.s.schedStartMin = 22 * 60; s.s.schedEndMin = 6 * 60; s.begin();
  s.in.timeValid = true; s.in.localMinutes = 12 * 60;      // noon: outside an overnight window
  needs(s, true, false, false);
  s.run(10000);
  CHECK(s.c.status().state == RS_IDLE && s.c.status().outsideSchedule);
  s.in.localMinutes = 23 * 60;
  s.run(6000);
  CHECK(s.c.status().target == 0);
  s.in.localMinutes = 7 * 60;                              // window closes mid-fill
  s.run(3000);
  CHECK(s.c.status().state == RS_IDLE);
  s.in.timeValid = false;                                  // no clock: do not block fills
  s.run(8000);
  CHECK(s.c.status().target == 0);
}

static void test_restored_fault() {
  printf("latched fault survives a restart\n");
  Sim s; g_sim = &s; s.begin();
  s.c.restoreFaults(0b010);                                // Brian's tank was faulted before the reboot
  needs(s, true, true, false);
  s.run(9000);
  CHECK(s.c.status().fault[1] && !s.c.status().fault[0]);
  CHECK(s.c.status().target == 0);                         // CPAP still fills
  needs(s, false, true, false);
  s.run(20000);
  CHECK(s.c.status().state == RS_IDLE && !s.c.outputs().pump);   // Brian stays blocked until cleared
  s.c.cmdClearFault(1);
  s.run(9000);
  CHECK(s.c.status().target == 1);
}

static void test_iofault() {
  printf("i/o fault\n");
  Sim s; g_sim = &s; s.s.pumpOnSec = 600; s.begin();
  needs(s, true, false, false);
  s.run(9000);
  CHECK(s.c.outputs().pump);
  s.in.ioFault = true;
  s.run(300);
  CHECK(!s.c.outputs().pump);
  s.run(3000);
  CHECK(s.c.status().state == RS_IDLE);
  s.run(5000);
  CHECK(s.c.status().state == RS_IDLE && s.c.status().ioFault);
  s.in.ioFault = false;
  s.run(9000);
  CHECK(s.c.status().target == 0);
}

static void test_invert() {
  printf("polarity invert\n");
  Sim s; g_sim = &s; s.s.tankInvert[0] = true; s.begin();
  needs(s, false, false, false);                           // inverted: inactive input = needs water
  s.run(9000);
  CHECK(s.c.status().target == 0);
  needs(s, true, false, false);
  s.run(6000);
  CHECK(s.c.status().state == RS_IDLE);
}

static void test_fuzz(unsigned seed) {
  Sim s; g_sim = &s; srand(seed);
  s.s.valveLeadMs = 300 + rand() % 2000; s.s.valveLagMs = 300 + rand() % 2000;
  s.s.pumpOnSec = 1 + rand() % 15; s.s.pumpOffSec = rand() % 12;
  s.s.tankMaxPumpSec[0] = rand() % 40; s.s.manualMaxSec = 10 + rand() % 30; s.s.ozoneRunSec = 1 + rand() % 20;
  s.s.levelDebounceMs = rand() % 1500;
  s.begin();
  for (int i = 0; i < 40000; i++) {                        // ~33 minutes of simulated time
    if (rand() % 40 == 0) needs(s, rand() & 1, rand() & 1, rand() & 1);
    if (rand() % 200 == 0) s.in.supplyActive = rand() & 1;
    if (rand() % 400 == 0) s.in.ozoneButton = rand() & 1;
    if (rand() % 1500 == 0) s.in.ioFault = rand() & 1;
    if (rand() % 300 == 0) s.c.cmdMode(rand() & 1);
    if (rand() % 500 == 0) s.c.cmdStop();
    if (rand() % 100 == 0) s.c.cmdRun(rand() % 3);
    if (rand() % 700 == 0) s.c.cmdOzone();
    if (rand() % 900 == 0) s.c.cmdClearFault(rand() % 3);
    if (rand() % 1200 == 0) { Settings t = s.c.settings(); t.tankEnabled[rand() % 3] = rand() & 1; t.supplyEnabled = rand() & 1; s.c.setSettings(t); }
    s.tick();
  }
}

static void test_history() {
  printf("history\n");
  History h;
  for (int i = 0; i < 400; i++) { Event e{}; e.t = 1000 + i; e.type = EV_FILL; e.arg = i % 3; e.a = i; h.addEvent(e); }
  CHECK(h.eventCount() == History::EV_MAX);
  CHECK(h.event(0).t == 1000 + 400 - History::EV_MAX);      // oldest surviving
  CHECK(h.event(h.eventCount() - 1).t == 1399);
  for (int d = 0; d < 200; d++) { h.day(20000 + d).pumpSec += d; h.day(20000 + d).fills[1]++; }
  CHECK(h.dayCount() == History::DAY_MAX);
  CHECK(h.dayAt(h.dayCount() - 1).day == 20199);
  h.day(20199).pumpSec += 5;                                // same day again: no new row
  CHECK(h.dayCount() == History::DAY_MAX);
  static uint8_t buf[History::MAX_BYTES];
  size_t n = h.serialize(buf, sizeof buf);
  CHECK(n > 0 && n <= sizeof buf);
  History g;
  CHECK(g.deserialize(buf, n));
  CHECK(g.eventCount() == h.eventCount() && g.dayCount() == h.dayCount());
  CHECK(g.event(5).t == h.event(5).t && g.dayAt(7).pumpSec == h.dayAt(7).pumpSec);
  buf[n / 2] ^= 0xFF;                                       // corruption is rejected
  History z;
  CHECK(!z.deserialize(buf, n));
  CHECK(!z.deserialize(buf, 10));
}

int main() {
  test_basic_fill();
  test_priority_and_routes();
  test_disabled_tank();
  test_pump_governor();
  test_supply_interlock();
  test_fault_latch();
  test_mode_and_stop();
  test_manual();
  test_ozone();
  test_schedule();
  test_iofault();
  test_restored_fault();
  test_invert();
  printf("fuzz\n");
  for (unsigned seed = 1; seed <= 40; seed++) test_fuzz(seed);
  test_history();
  printf("\n%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
