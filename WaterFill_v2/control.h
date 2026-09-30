// Fill/ozone controller. Pure logic: time and I/O are passed in, so it is unit-tested on a PC.
//
// Plumbing: pump -> V1 -> V2 -> (Dina). V1 energised = CPAP. V1 off + V2 energised = Brian.
// Both off = Dina. So at least one route is always open and the pump never dead-heads.
#pragma once
#include <stdint.h>
#include "settings.h"

namespace wf {

enum RunState : uint8_t { RS_IDLE, RS_OPENING, RS_FILLING, RS_STOPPING, RS_OZONE };

enum StopReason : uint8_t {
  SR_FULL, SR_DISABLED, SR_SCHEDULE, SR_USER, SR_MODE, SR_FAULT, SR_MANUAL_TIMEOUT, SR_IOFAULT
};

enum EventType : uint8_t {
  EV_BOOT = 1,
  EV_FILL,        // arg tank, a = seconds, b = pump seconds, pad = reason | manual << 8
  EV_FILL_FAULT,  // arg tank, a = pump seconds
  EV_SUPPLY,      // arg 1 = empty, 0 = restored
  EV_OZONE,       // a = seconds
  EV_MODE,        // arg 1 = auto
  EV_STOP,        // user stop
  EV_CLEAR,       // arg tank
  EV_SETTINGS,
  EV_OTA,
  EV_IOFAULT      // arg 1 = fault, 0 = cleared
};

struct Inputs {
  bool tankActive[N_TANKS];  // electrical: input pulled LOW
  bool supplyActive;
  bool ozoneButton;
  bool ioFault;              // I2C expander not answering
  bool timeValid;
  uint16_t localMinutes;     // minutes after local midnight
};

struct Outputs { bool pump, v1, v2, ozone; };

struct Status {
  bool autoMode;
  RunState state;
  int8_t target;             // -1 when no fill in progress
  bool manualRun;
  bool need[N_TANKS];
  bool fault[N_TANKS];
  bool supplyEmpty;
  bool ioFault;
  bool outsideSchedule;      // auto mode, a tank needs water, window is closed
  bool waitSupply;           // fill in progress but supply empty
  bool ozonePending;
  Outputs out;
  uint16_t coolRemS;         // pump cool-down remaining
  uint16_t stintS;           // continuous pump seconds this stint
  uint16_t fillElapsedS;
  uint16_t fillPumpS;
  uint16_t fillLimitS;       // pump-seconds limit of this fill (0 = none)
  uint16_t manualRemS;
  uint16_t ozoneRemS;
};

class Controller {
 public:
  typedef void (*EventFn)(void* ctx, uint8_t type, uint8_t arg, uint16_t a, uint16_t b, uint16_t pad);

  void begin(uint32_t now, const Settings& s, EventFn fn, void* ctx);
  void update(uint32_t now, const Inputs& in);
  void setSettings(const Settings& s);
  const Settings& settings() const { return cfg_; }
  const Outputs& outputs() const { return out_; }
  const Status& status() const { return st_; }
  bool takeSettingsDirty() { bool d = dirty_; dirty_ = false; return d; }

  bool cmdMode(bool autoMode);   // stops everything first
  void cmdStop();                // stops everything and switches to Manual
  bool cmdRun(int tank);         // Manual only
  bool cmdOzone();
  void cmdCancelOzone();         // cancels a running or pending ozone run only
  void cmdClearFault(int tank);
  void restoreFaults(uint32_t mask) { for (int i = 0; i < N_TANKS; i++) fault_[i] = (mask >> i) & 1; }

 private:
  struct Deb { bool primed = false, stable = false, cand = false; uint32_t since = 0; };
  static bool deb(Deb& d, bool raw, uint32_t now, uint32_t toTrueMs, uint32_t toFalseMs);

  void emit(uint8_t type, uint8_t arg = 0, uint16_t a = 0, uint16_t b = 0, uint16_t pad = 0);
  void setValves(int tank);
  void startFill(int tank, bool manual, uint32_t now);
  void beginStop(StopReason why, uint32_t now);
  void requestStop(StopReason why) { stopReq_ = true; stopWhy_ = why; }
  void startOzone(uint32_t now);
  bool schedOk(const Inputs& in) const;
  void finalize(uint32_t now, const Inputs& in);

  Settings cfg_{};
  Outputs out_{};
  Status st_{};
  EventFn fn_ = nullptr;
  void* ctx_ = nullptr;
  bool dirty_ = false;
  bool stopReq_ = false;
  StopReason stopWhy_ = SR_USER;

  RunState state_ = RS_IDLE;
  uint32_t stateSince_ = 0, last_ = 0, holdUntil_ = 0, now_ = 0;
  int target_ = -1;
  bool manual_ = false;
  uint32_t sessionStart_ = 0, fillPumpMs_ = 0;
  int manualReq_ = -1;
  bool ozonePending_ = false;
  bool fault_[N_TANKS] = {};
  bool need_[N_TANKS] = {};
  bool supplyEmpty_ = false;
  bool ioFault_ = false;
  bool btnPrev_ = false;
  Deb dbT_[N_TANKS], dbS_, dbB_;

  uint32_t stintMs_ = 0, offAccMs_ = 0, coolUntil_ = 0;
  bool cooling_ = false;
};

}  // namespace wf
