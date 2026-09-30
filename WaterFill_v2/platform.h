// Hardware / storage / time glue for the KC868-A6. Everything Arduino-specific lives here.
#pragma once
#include <Arduino.h>
#include "settings.h"
#include "control.h"
#include "history.h"
#include "life.h"

namespace plat {

// logging (Serial + small RAM ring for the web UI)
void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
size_t logDump(char* out, size_t cap);

// I/O expanders. Inputs/relays are active-low; outputs are forced OFF at boot.
void ioBegin();
void ioPoll(wf::Inputs& in);                   // reads the input expander into `in`
void ioWrite(const wf::Outputs& o);            // writes relays (only on change, plus a refresh)
void ioAllOff();
bool ioFault();
uint8_t rawDi();
uint8_t rawDo();

// persistence
bool storageBegin();
bool loadSettings(wf::Settings& s);
void saveSettings(const wf::Settings& s);
bool loadHistory(wf::History& h);
void saveHistory(const wf::History& h);
bool loadLife(wf::Life& l);
void saveLife(const wf::Life& l);

// time: NTP with DS1307 fallback (the RTC holds UTC, marked by a RAM byte)
void timeRestoreRtc();
void timeStartNtp(const char* tz);
void timeSetTz(const char* tz);
void timeService();                            // call about once a second
bool timeValid();
uint32_t epoch();                              // 0 if the clock is not set
uint16_t localMinutes();
uint16_t localDay();                           // local days since 1970-01-01

}  // namespace plat
