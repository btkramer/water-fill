// Lifetime counters and latched faults, kept in NVS so they survive reboots and updates.
#pragma once
#include <stdint.h>

namespace wf {

struct Life {
  uint32_t magic;          // LIFE_MAGIC when valid
  uint32_t pumpSec;        // total seconds the pump relay was on
  uint32_t ozoneSec;
  uint32_t pumpCycles;     // relay starts (wear indicator)
  uint32_t faults;         // fill faults ever raised
  uint32_t fills[3];       // completed fills per tank
  uint32_t faultMask;      // bit per tank: fault currently latched
};

static const uint32_t LIFE_MAGIC = 0x4C494645;

}  // namespace wf
