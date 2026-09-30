#include "history.h"
#include <string.h>

namespace wf {

static_assert(sizeof(Event) == 12, "Event layout");
static_assert(sizeof(Daily) == 16, "Daily layout");

static const uint32_t MAGIC = 0x31484657;  // "WFH1"

static uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; i++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

void History::addEvent(const Event& e) {
  int slot = (evHead_ + evCount_) % EV_MAX;
  ev_[slot] = e;
  if (evCount_ < EV_MAX) evCount_++;
  else evHead_ = (evHead_ + 1) % EV_MAX;
}

const Event& History::event(int i) const { return ev_[(evHead_ + i) % EV_MAX]; }

Daily& History::day(uint16_t dayIndex) {
  for (int i = dayCount_ - 1; i >= 0 && i >= dayCount_ - 3; i--)
    if (days_[i].day == dayIndex) return days_[i];
  if (dayCount_ == DAY_MAX) {
    memmove(&days_[0], &days_[1], (DAY_MAX - 1) * sizeof(Daily));
    dayCount_--;
  }
  Daily& d = days_[dayCount_++];
  memset(&d, 0, sizeof d);
  d.day = dayIndex;
  return d;
}

// layout: magic u32, evCount u16, dayCount u16, events (oldest first), days, crc32 of everything before it
size_t History::serialize(uint8_t* buf, size_t cap) const {
  size_t need = 8 + (size_t)evCount_ * sizeof(Event) + (size_t)dayCount_ * sizeof(Daily) + 4;
  if (cap < need) return 0;
  uint8_t* p = buf;
  memcpy(p, &MAGIC, 4); p += 4;
  uint16_t ec = (uint16_t)evCount_, dc = (uint16_t)dayCount_;
  memcpy(p, &ec, 2); p += 2;
  memcpy(p, &dc, 2); p += 2;
  for (int i = 0; i < evCount_; i++) { Event e = event(i); memcpy(p, &e, sizeof e); p += sizeof e; }
  memcpy(p, days_, (size_t)dayCount_ * sizeof(Daily)); p += (size_t)dayCount_ * sizeof(Daily);
  uint32_t c = crc32(buf, (size_t)(p - buf));
  memcpy(p, &c, 4); p += 4;
  return (size_t)(p - buf);
}

bool History::deserialize(const uint8_t* buf, size_t len) {
  if (len < 12) return false;
  uint32_t magic; memcpy(&magic, buf, 4);
  uint16_t ec, dc; memcpy(&ec, buf + 4, 2); memcpy(&dc, buf + 6, 2);
  if (magic != MAGIC || ec > EV_MAX || dc > DAY_MAX) return false;
  size_t need = 8 + (size_t)ec * sizeof(Event) + (size_t)dc * sizeof(Daily) + 4;
  if (len != need) return false;
  uint32_t want; memcpy(&want, buf + need - 4, 4);
  if (crc32(buf, need - 4) != want) return false;
  evHead_ = 0; evCount_ = ec; dayCount_ = dc;
  memcpy(ev_, buf + 8, (size_t)ec * sizeof(Event));
  memcpy(days_, buf + 8 + (size_t)ec * sizeof(Event), (size_t)dc * sizeof(Daily));
  return true;
}

}  // namespace wf
