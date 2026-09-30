// All operator-adjustable setpoints. Plain data, no Arduino dependencies.
#pragma once
#include <stdint.h>

namespace wf {

constexpr int N_TANKS = 3;              // 0 = CPAP, 1 = Brian's Dock Pro, 2 = Dina's Dock Pro
constexpr uint8_t SETTINGS_VERSION = 1;

struct Settings {
  uint8_t  version;
  bool     autoMode;                    // true = Auto, false = Manual
  bool     tankEnabled[N_TANKS];        // disabled tank: never checked, never auto-filled
  bool     tankInvert[N_TANKS];         // false: input LOW (active) = tank needs water
  bool     supplyEnabled;               // use the supply-tank low switch as a pump interlock
  bool     supplyInvert;                // false: input LOW (active) = supply tank empty
  bool     ozoneEnabled;
  bool     schedEnabled;                // restrict automatic fills to a daily window
  uint16_t tankMaxPumpSec[N_TANKS];     // pump-on seconds allowed per fill before a fault (0 = off)
  uint16_t pumpOnSec;                   // max continuous pump run
  uint16_t pumpOffSec;                  // cool-down after a max run
  uint16_t valveLeadMs;                 // valve in position this long before the pump starts
  uint16_t valveLagMs;                  // pump stopped this long before valves move or release
  uint16_t levelDebounceMs;             // tank switch must hold this long to change state
  uint16_t supplyTripS;                 // supply must read empty this long before the pump is blocked
  uint16_t supplyRecoverS;              // and read OK this long before it may run again
  uint16_t schedStartMin;               // minutes after local midnight
  uint16_t schedEndMin;
  uint16_t ozoneRunSec;
  uint16_t manualMaxSec;                // manual fills stop by themselves after this long
  char     tz[48];                      // POSIX TZ string
  char     pin[12];                     // empty = no PIN on control actions / OTA
};

void settingsDefaults(Settings& s);
void settingsClamp(Settings& s);

}  // namespace wf
