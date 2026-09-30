// Event log and per-day totals. Pure data structure (no Arduino dependencies).
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace wf {

struct Event {              // 12 bytes
  uint32_t t;               // epoch seconds (0 = clock not set)
  uint8_t type, arg;
  uint16_t a, b, pad;       // meaning depends on type, see EventType in control.h
};

struct Daily {              // 16 bytes
  uint16_t day;             // local days since 1970-01-01
  uint8_t fills[3];
  uint8_t ozoneRuns;
  uint32_t pumpSec;
  uint16_t ozoneSec;
  uint8_t faults, pad;
};

class History {
 public:
  static const int EV_MAX = 300;
  static const int DAY_MAX = 120;
  static const size_t MAX_BYTES = 12 + EV_MAX * sizeof(Event) + DAY_MAX * sizeof(Daily);

  void addEvent(const Event& e);
  int eventCount() const { return evCount_; }
  const Event& event(int i) const;            // 0 = oldest

  Daily& day(uint16_t dayIndex);              // finds or creates today's row
  int dayCount() const { return dayCount_; }
  const Daily& dayAt(int i) const { return days_[i]; }

  size_t serialize(uint8_t* buf, size_t cap) const;
  bool deserialize(const uint8_t* buf, size_t len);

 private:
  Event ev_[EV_MAX];
  Daily days_[DAY_MAX];
  int evHead_ = 0, evCount_ = 0, dayCount_ = 0;
};

}  // namespace wf
