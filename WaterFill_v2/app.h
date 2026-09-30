// State shared between the control loop and the web server tasks. Guarded by app.lock.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "control.h"
#include "history.h"
#include "life.h"
#include "settings.h"

enum CmdType : uint8_t { CMD_AUTO = 1, CMD_MANUAL, CMD_STOP, CMD_RUN, CMD_OZONE, CMD_CLEAR, CMD_OZONE_STOP };
struct Cmd { uint8_t type; int8_t arg; };

#define FW_VERSION "2.0.0-dev"

struct App {
  SemaphoreHandle_t lock = nullptr;

  // owned by the loop task; other tasks only read the mirrors below, under the lock
  wf::Controller ctl;
  wf::History hist;

  wf::Status status{};
  wf::Settings cfg{};
  uint8_t rawDi = 0xFF, rawDo = 0xFF;
  bool timeValid = false;
  uint32_t epoch = 0;
  uint32_t lastFill[wf::N_TANKS] = {};
  uint32_t statusVersion = 0;
  wf::Life life{};
  bool lifeDirty = false, lifeUrgent = false;

  // requests from the web server, applied by the loop task
  Cmd q[8];
  uint8_t qn = 0;
  bool cfgPending = false;
  wf::Settings cfgNew{};

  volatile bool otaBusy = false;
  volatile bool rebootPending = false;
  volatile uint32_t rebootAt = 0;
  bool histDirty = false, histUrgent = false;
};

extern App app;

// Queue a command for the control loop (any task).
inline void queueCmd(uint8_t type, int8_t arg) {
  xSemaphoreTake(app.lock, portMAX_DELAY);
  if (app.qn < sizeof app.q / sizeof app.q[0]) app.q[app.qn++] = Cmd{type, arg};
  xSemaphoreGive(app.lock);
}

// Change settings from any task: fn edits a copy of the latest (pending or current) settings.
template <typename F> void changeSettings(F fn) {
  xSemaphoreTake(app.lock, portMAX_DELAY);
  wf::Settings n = app.cfgPending ? app.cfgNew : app.cfg;
  fn(n);
  wf::settingsClamp(n);
  app.cfgNew = n;
  app.cfgPending = true;
  xSemaphoreGive(app.lock);
}

struct Lock {
  Lock() { xSemaphoreTake(app.lock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(app.lock); }
};
