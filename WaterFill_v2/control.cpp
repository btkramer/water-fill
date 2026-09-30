#include "control.h"

namespace wf {

static inline uint32_t ms(uint16_t s) { return (uint32_t)s * 1000u; }
static inline uint16_t cap16(uint32_t v) { return v > 65535u ? 65535u : (uint16_t)v; }
static const uint32_t BOOT_HOLD_MS = 3000;  // no automatic start until inputs have settled

bool Controller::deb(Deb& d, bool raw, uint32_t now, uint32_t toTrueMs, uint32_t toFalseMs) {
  if (!d.primed) { d.primed = true; d.stable = d.cand = raw; d.since = now; return d.stable; }  // trust the first sample
  if (raw != d.cand) { d.cand = raw; d.since = now; }
  if (d.cand != d.stable && (uint32_t)(now - d.since) >= (d.cand ? toTrueMs : toFalseMs)) d.stable = d.cand;
  return d.stable;
}

void Controller::begin(uint32_t now, const Settings& s, EventFn fn, void* ctx) {
  cfg_ = s;
  settingsClamp(cfg_);
  fn_ = fn;
  ctx_ = ctx;
  now_ = last_ = stateSince_ = now;
  holdUntil_ = now + BOOT_HOLD_MS;
  out_ = Outputs{};
  state_ = RS_IDLE;
  target_ = -1;
  manualReq_ = -1;
  ozonePending_ = false;
  dbS_ = dbB_ = Deb();
  for (int i = 0; i < N_TANKS; i++) { dbT_[i] = Deb(); fault_[i] = need_[i] = false; }
  stopReq_ = false;
  stintMs_ = offAccMs_ = 0;
  cooling_ = false;
  st_ = Status{};
  st_.autoMode = cfg_.autoMode;
  st_.target = -1;
}

void Controller::emit(uint8_t type, uint8_t arg, uint16_t a, uint16_t b, uint16_t pad) {
  if (fn_) fn_(ctx_, type, arg, a, b, pad);
}

void Controller::setValves(int tank) {
  out_.v1 = (tank == 0);
  out_.v2 = (tank == 1);
}

bool Controller::schedOk(const Inputs& in) const {
  if (!cfg_.schedEnabled || !in.timeValid) return true;
  uint16_t m = in.localMinutes, a = cfg_.schedStartMin, b = cfg_.schedEndMin;
  if (a == b) return true;
  return a < b ? (m >= a && m < b) : (m >= a || m < b);
}

void Controller::startFill(int tank, bool manual, uint32_t now) {
  target_ = tank;
  manual_ = manual;
  sessionStart_ = now;
  fillPumpMs_ = 0;
  state_ = RS_OPENING;
  stateSince_ = now;
  out_.pump = false;
  setValves(tank);
}

void Controller::startOzone(uint32_t now) {
  ozonePending_ = false;
  state_ = RS_OZONE;
  stateSince_ = now;
  out_.pump = false;
  out_.v1 = out_.v2 = false;
  out_.ozone = true;
}

void Controller::beginStop(StopReason why, uint32_t now) {
  if (state_ == RS_OPENING || state_ == RS_FILLING) {
    if (fillPumpMs_ >= 500 || why == SR_FAULT)
      emit(EV_FILL, (uint8_t)target_, cap16((now - sessionStart_) / 1000), cap16(fillPumpMs_ / 1000),
           (uint16_t)(why | (manual_ ? 0x100 : 0)));
  } else if (state_ == RS_OZONE) {
    emit(EV_OZONE, 0, cap16((now - stateSince_) / 1000));
  } else {
    return;  // idle or already stopping: nothing to do
  }
  out_.pump = false;  // pump first; valves follow after valveLagMs
  out_.ozone = false;
  state_ = RS_STOPPING;
  stateSince_ = now;
}

bool Controller::cmdMode(bool autoMode) {
  if (autoMode == cfg_.autoMode) return true;
  requestStop(SR_MODE);
  manualReq_ = -1;
  ozonePending_ = false;
  cfg_.autoMode = autoMode;
  dirty_ = true;
  emit(EV_MODE, autoMode ? 1 : 0);
  return true;
}

void Controller::cmdStop() {
  requestStop(SR_USER);
  manualReq_ = -1;
  ozonePending_ = false;
  if (cfg_.autoMode) {  // a stop in Auto would just restart at once, so Stop also leaves Auto
    cfg_.autoMode = false;
    dirty_ = true;
    emit(EV_MODE, 0);
  }
  emit(EV_STOP);
}

bool Controller::cmdRun(int tank) {
  if (cfg_.autoMode || tank < 0 || tank >= N_TANKS) return false;
  ozonePending_ = false;
  if (state_ == RS_OPENING || state_ == RS_FILLING) {
    if (manual_ && target_ == tank && !stopReq_) return true;
  }
  if (state_ == RS_OZONE || state_ == RS_OPENING || state_ == RS_FILLING) requestStop(SR_USER);
  manualReq_ = tank;
  return true;
}

bool Controller::cmdOzone() {
  if (!cfg_.ozoneEnabled) return false;
  if (state_ == RS_OZONE) return true;
  if (!cfg_.autoMode && (state_ == RS_OPENING || state_ == RS_FILLING)) requestStop(SR_USER);
  manualReq_ = -1;
  ozonePending_ = true;
  return true;
}

void Controller::cmdCancelOzone() {
  ozonePending_ = false;
  if (state_ == RS_OZONE) requestStop(SR_USER);
}

void Controller::cmdClearFault(int tank) {
  if (tank < 0 || tank >= N_TANKS || !fault_[tank]) return;
  fault_[tank] = false;
  emit(EV_CLEAR, (uint8_t)tank);
}

void Controller::setSettings(const Settings& s) {
  Settings n = s;
  settingsClamp(n);
  n.version = SETTINGS_VERSION;
  bool wantAuto = n.autoMode;
  n.autoMode = cfg_.autoMode;  // mode only changes through cmdMode
  cfg_ = n;
  if (wantAuto != cfg_.autoMode) cmdMode(wantAuto);
  if (!cfg_.ozoneEnabled) ozonePending_ = false;
  dirty_ = true;
  emit(EV_SETTINGS);
}

void Controller::update(uint32_t now, const Inputs& in) {
  uint32_t dt = now - last_;
  last_ = now_ = now;
  if (dt > 500) dt = 500;

  // --- inputs ---
  for (int t = 0; t < N_TANKS; t++)
    need_[t] = deb(dbT_[t], in.tankActive[t] != cfg_.tankInvert[t], now, cfg_.levelDebounceMs, cfg_.levelDebounceMs);
  bool empty = false;
  if (cfg_.supplyEnabled)
    empty = deb(dbS_, in.supplyActive != cfg_.supplyInvert, now, ms(cfg_.supplyTripS), ms(cfg_.supplyRecoverS));
  else dbS_ = Deb();
  if (empty != supplyEmpty_) { supplyEmpty_ = empty; emit(EV_SUPPLY, empty ? 1 : 0); }
  bool btn = deb(dbB_, in.ozoneButton, now, 50, 50);
  if (btn && !btnPrev_) cmdOzone();
  btnPrev_ = btn;
  if (in.ioFault != ioFault_) { ioFault_ = in.ioFault; emit(EV_IOFAULT, ioFault_ ? 1 : 0); }

  // --- pump bookkeeping: continuous-run time survives a tank change, resets after a real rest ---
  uint32_t onMs = ms(cfg_.pumpOnSec), offMs = ms(cfg_.pumpOffSec);
  if (out_.pump) {
    stintMs_ += dt;
    offAccMs_ = 0;
    if (state_ == RS_FILLING) fillPumpMs_ += dt;
  } else {
    offAccMs_ += dt;
    if (offAccMs_ >= offMs) stintMs_ = 0;
  }
  if (cooling_ && (int32_t)(now - coolUntil_) >= 0) { cooling_ = false; stintMs_ = 0; }  // rested
  if (out_.pump && stintMs_ >= onMs) {  // max continuous run, whatever state we are in
    if (offMs == 0) stintMs_ = 0;
    else { out_.pump = false; cooling_ = true; coolUntil_ = now + offMs; }
  }

  if (stopReq_) { stopReq_ = false; beginStop(stopWhy_, now); }
  if (ioFault_ && (state_ == RS_OPENING || state_ == RS_FILLING || state_ == RS_OZONE))
    beginStop(SR_IOFAULT, now);

  // A running fill should end if any of these holds; returns true after calling beginStop.
  auto sessionStop = [&]() -> bool {
    StopReason why;
    if (manual_) {
      if ((uint32_t)(now - sessionStart_) < ms(cfg_.manualMaxSec)) return false;
      why = SR_MANUAL_TIMEOUT;
    } else if (!cfg_.autoMode) why = SR_MODE;
    else if (!cfg_.tankEnabled[target_]) why = SR_DISABLED;
    else if (fault_[target_]) why = SR_FAULT;
    else if (!need_[target_]) why = SR_FULL;
    else if (!schedOk(in)) why = SR_SCHEDULE;
    else return false;
    beginStop(why, now);
    return true;
  };

  // --- state machine ---
  switch (state_) {
    case RS_IDLE:
      out_.pump = out_.ozone = out_.v1 = out_.v2 = false;
      if ((int32_t)(now - holdUntil_) < 0 || ioFault_) break;
      if (ozonePending_) {
        if (cfg_.ozoneEnabled) startOzone(now); else ozonePending_ = false;
      } else if (manualReq_ >= 0) {
        int t = manualReq_;
        manualReq_ = -1;
        startFill(t, true, now);
      } else if (cfg_.autoMode && !supplyEmpty_ && schedOk(in)) {
        for (int t = 0; t < N_TANKS; t++) {
          if (cfg_.tankEnabled[t] && need_[t] && !fault_[t]) { startFill(t, false, now); break; }
        }
      }
      break;

    case RS_OPENING:
      out_.pump = false;
      setValves(target_);
      if (sessionStop()) break;
      if ((uint32_t)(now - stateSince_) >= cfg_.valveLeadMs) { state_ = RS_FILLING; stateSince_ = now; }
      break;

    case RS_FILLING: {
      setValves(target_);
      if (sessionStop()) break;
      bool want = !supplyEmpty_ && !ioFault_;
      if (out_.pump) {
        if (!want) out_.pump = false;
      } else if (want && !cooling_) {
        out_.pump = true;
      }
      if (!manual_ && cfg_.tankMaxPumpSec[target_] && fillPumpMs_ >= ms(cfg_.tankMaxPumpSec[target_])) {
        fault_[target_] = true;
        emit(EV_FILL_FAULT, (uint8_t)target_, cap16(fillPumpMs_ / 1000));
        beginStop(SR_FAULT, now);
      }
      break;
    }

    case RS_STOPPING:
      out_.pump = out_.ozone = false;
      if ((uint32_t)(now - stateSince_) >= cfg_.valveLagMs) {
        out_.v1 = out_.v2 = false;
        state_ = RS_IDLE;
        target_ = -1;
        stateSince_ = now;
      }
      break;

    case RS_OZONE:
      out_.pump = out_.v1 = out_.v2 = false;
      out_.ozone = true;
      if (!cfg_.ozoneEnabled) { beginStop(SR_USER, now); break; }
      if ((uint32_t)(now - stateSince_) >= ms(cfg_.ozoneRunSec)) {
        emit(EV_OZONE, 0, cfg_.ozoneRunSec);
        out_.ozone = false;
        state_ = RS_IDLE;
        stateSince_ = now;
      }
      break;
  }

  finalize(now, in);
}

void Controller::finalize(uint32_t now, const Inputs& in) {
  Status& s = st_;
  s = Status{};
  s.autoMode = cfg_.autoMode;
  s.state = state_;
  bool filling = (state_ == RS_OPENING || state_ == RS_FILLING ||
                  (state_ == RS_STOPPING && target_ >= 0));
  s.target = filling ? (int8_t)target_ : -1;
  s.manualRun = filling && manual_;
  bool anyNeed = false;
  for (int t = 0; t < N_TANKS; t++) {
    s.need[t] = need_[t];
    s.fault[t] = fault_[t];
    if (cfg_.tankEnabled[t] && need_[t] && !fault_[t]) anyNeed = true;
  }
  s.supplyEmpty = supplyEmpty_;
  s.ioFault = ioFault_;
  s.outsideSchedule = cfg_.autoMode && anyNeed && !schedOk(in);
  s.waitSupply = (state_ == RS_FILLING) && supplyEmpty_;
  s.ozonePending = ozonePending_;
  s.out = out_;
  if (cooling_ && (int32_t)(coolUntil_ - now) > 0) s.coolRemS = cap16((coolUntil_ - now + 999) / 1000);
  s.stintS = cap16(stintMs_ / 1000);
  if (filling) {
    s.fillElapsedS = cap16((now - sessionStart_) / 1000);
    s.fillPumpS = cap16(fillPumpMs_ / 1000);
    s.fillLimitS = manual_ ? 0 : cfg_.tankMaxPumpSec[target_];
    if (manual_) {
      uint32_t el = now - sessionStart_, mx = ms(cfg_.manualMaxSec);
      s.manualRemS = cap16(el < mx ? (mx - el + 999) / 1000 : 0);
    }
  }
  if (state_ == RS_OZONE) {
    uint32_t el = now - stateSince_, mx = ms(cfg_.ozoneRunSec);
    s.ozoneRemS = cap16(el < mx ? (mx - el + 999) / 1000 : 0);
  }
}

}  // namespace wf
