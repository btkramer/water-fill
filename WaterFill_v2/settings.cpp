#include "settings.h"
#include <string.h>

namespace wf {

void settingsDefaults(Settings& s) {
  memset(&s, 0, sizeof s);
  s.version = SETTINGS_VERSION;
  s.autoMode = true;
  for (int i = 0; i < N_TANKS; i++) {
    s.tankEnabled[i] = true;
    s.tankInvert[i] = false;
    s.tankMaxPumpSec[i] = 600;
  }
  s.supplyEnabled = true;
  s.supplyInvert = false;
  s.ozoneEnabled = true;
  s.schedEnabled = false;
  s.pumpOnSec = 10;
  s.pumpOffSec = 10;
  s.valveLeadMs = 1000;
  s.valveLagMs = 1000;
  s.levelDebounceMs = 2000;
  s.supplyTripS = 2;
  s.supplyRecoverS = 10;
  s.schedStartMin = 8 * 60;
  s.schedEndMin = 21 * 60;
  s.ozoneRunSec = 60;
  s.manualMaxSec = 300;
  strcpy(s.tz, "MST7");
}

static uint16_t clampU(uint16_t v, uint16_t lo, uint16_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

void settingsClamp(Settings& s) {
  for (int i = 0; i < N_TANKS; i++) s.tankMaxPumpSec[i] = clampU(s.tankMaxPumpSec[i], 0, 7200);
  s.pumpOnSec = clampU(s.pumpOnSec, 1, 600);
  s.pumpOffSec = clampU(s.pumpOffSec, 0, 600);
  s.valveLeadMs = clampU(s.valveLeadMs, 0, 10000);
  s.valveLagMs = clampU(s.valveLagMs, 0, 10000);
  s.levelDebounceMs = clampU(s.levelDebounceMs, 0, 10000);
  s.supplyTripS = clampU(s.supplyTripS, 0, 60);
  s.supplyRecoverS = clampU(s.supplyRecoverS, 0, 600);
  s.schedStartMin = clampU(s.schedStartMin, 0, 1439);
  s.schedEndMin = clampU(s.schedEndMin, 0, 1439);
  s.ozoneRunSec = clampU(s.ozoneRunSec, 1, 1800);
  s.manualMaxSec = clampU(s.manualMaxSec, 10, 3600);
  s.tz[sizeof s.tz - 1] = 0;
  s.pin[sizeof s.pin - 1] = 0;
  if (!s.tz[0]) strcpy(s.tz, "MST7");
}

}  // namespace wf
