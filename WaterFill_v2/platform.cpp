#include "platform.h"
#include <Wire.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <esp_sntp.h>

namespace plat {

static const int SDA_PIN = 4, SCL_PIN = 15;
static const uint8_t ADDR_DI = 0x22, ADDR_DO = 0x24, ADDR_RTC = 0x68;
static const uint8_t OUT_OFF = 0xFF;

// ---------------------------------------------------------------- logging
static const int LOG_LINES = 40, LOG_LEN = 110;
static char g_log[LOG_LINES][LOG_LEN];
static uint32_t g_logN = 0;
static portMUX_TYPE g_logMux = portMUX_INITIALIZER_UNLOCKED;

void log(const char* fmt, ...) {
  char line[LOG_LEN];
  int n = snprintf(line, sizeof line, "[%lus] ", (unsigned long)(millis() / 1000));
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line + n, sizeof line - n, fmt, ap);
  va_end(ap);
  Serial.println(line);
  portENTER_CRITICAL(&g_logMux);
  memcpy(g_log[g_logN % LOG_LINES], line, LOG_LEN);
  g_logN++;
  portEXIT_CRITICAL(&g_logMux);
}

size_t logDump(char* out, size_t cap) {
  size_t o = 0;
  uint32_t n = g_logN, first = n > LOG_LINES ? n - LOG_LINES : 0;
  for (uint32_t i = first; i < n; i++) {
    char line[LOG_LEN];
    portENTER_CRITICAL(&g_logMux);
    memcpy(line, g_log[i % LOG_LINES], LOG_LEN);
    portEXIT_CRITICAL(&g_logMux);
    size_t l = strnlen(line, LOG_LEN);
    if (o + l + 2 > cap) break;
    memcpy(out + o, line, l);
    o += l;
    out[o++] = '\n';
  }
  out[o] = 0;
  return o;
}

// ---------------------------------------------------------------- I/O
static uint8_t g_rawDi = 0xFF, g_rawDo = OUT_OFF, g_lastWritten = OUT_OFF;
static uint8_t g_failIn = 0, g_failOut = 0;
static uint32_t g_lastWriteMs = 0;

static bool pcfWrite(uint8_t addr, uint8_t v) {
  Wire.beginTransmission(addr);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

void ioBegin() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  Wire.setTimeOut(20);
  pcfWrite(ADDR_DO, OUT_OFF);  // the expander keeps its state across an ESP reset
  g_lastWritten = g_rawDo = OUT_OFF;
}

void ioPoll(wf::Inputs& in) {
  if (Wire.requestFrom(ADDR_DI, (uint8_t)1) == 1) {
    g_rawDi = Wire.read();
    g_failIn = 0;
  } else if (g_failIn < 255) {
    g_failIn++;
  }
  in.ozoneButton = !(g_rawDi & 0x02);        // DI1
  in.supplyActive = !(g_rawDi & 0x04);       // DI2
  in.tankActive[0] = !(g_rawDi & 0x08);      // DI3 CPAP
  in.tankActive[1] = !(g_rawDi & 0x10);      // DI4 Brian's Dock Pro
  in.tankActive[2] = !(g_rawDi & 0x20);      // DI5 Dina's Dock Pro
  in.ioFault = ioFault();
}

void ioWrite(const wf::Outputs& o) {
  uint8_t v = OUT_OFF;
  if (o.pump)  v &= ~0x01;                   // DO0
  if (o.v1)    v &= ~0x02;                   // DO1 solenoid 1 (CPAP)
  if (o.v2)    v &= ~0x04;                   // DO2 solenoid 2 (Brian)
  if (o.ozone) v &= ~0x10;                   // DO4
  uint32_t now = millis();
  if (v == g_lastWritten && now - g_lastWriteMs < 500) return;
  // A single expander write can't order the pump against the valves, so the controller keeps
  // them apart in time (valve lead/lag); here we only write what it asks for.
  if (pcfWrite(ADDR_DO, v)) {
    g_lastWritten = g_rawDo = v;
    g_failOut = 0;
  } else {
    if (g_failOut < 255) g_failOut++;
    g_lastWritten = ~v;                      // force a retry on the next call
  }
  g_lastWriteMs = now;
}

void ioAllOff() {
  wf::Outputs off{};
  ioWrite(off);
}

bool ioFault() { return g_failIn >= 5 || g_failOut >= 3; }
uint8_t rawDi() { return g_rawDi; }
uint8_t rawDo() { return g_rawDo; }

// ---------------------------------------------------------------- persistence
static Preferences prefs;
static bool g_fsOk = false;

bool storageBegin() {
  prefs.begin("waterfill", false);
  g_fsOk = LittleFS.begin(true);
  if (!g_fsOk) log("LittleFS mount failed");
  return g_fsOk;
}

bool loadSettings(wf::Settings& s) {
  wf::Settings t;
  if (prefs.getBytesLength("cfg") != sizeof t) return false;
  prefs.getBytes("cfg", &t, sizeof t);
  if (t.version != wf::SETTINGS_VERSION) return false;
  wf::settingsClamp(t);
  s = t;
  return true;
}

void saveSettings(const wf::Settings& s) { prefs.putBytes("cfg", &s, sizeof s); }

bool loadHistory(wf::History& h) {
  if (!g_fsOk) return false;
  File f = LittleFS.open("/hist.bin", "r");
  if (!f) return false;
  size_t n = f.size();
  if (n == 0 || n > wf::History::MAX_BYTES) { f.close(); return false; }
  uint8_t* buf = (uint8_t*)malloc(n);
  if (!buf) { f.close(); return false; }
  bool ok = f.read(buf, n) == (int)n && h.deserialize(buf, n);
  free(buf);
  f.close();
  return ok;
}

void saveHistory(const wf::History& h) {
  if (!g_fsOk) return;
  uint8_t* buf = (uint8_t*)malloc(wf::History::MAX_BYTES);
  if (!buf) return;
  size_t n = h.serialize(buf, wf::History::MAX_BYTES);
  if (n) {
    File f = LittleFS.open("/hist.tmp", "w");
    if (f) {
      bool ok = f.write(buf, n) == n;
      f.close();
      if (ok) { LittleFS.remove("/hist.bin"); LittleFS.rename("/hist.tmp", "/hist.bin"); }
    }
  }
  free(buf);
}

bool loadLife(wf::Life& l) {
  wf::Life t;
  if (prefs.getBytesLength("life") != sizeof t) return false;
  prefs.getBytes("life", &t, sizeof t);
  if (t.magic != wf::LIFE_MAGIC) return false;
  l = t;
  return true;
}

void saveLife(const wf::Life& l) { prefs.putBytes("life", &l, sizeof l); }

// ---------------------------------------------------------------- time
static const time_t MIN_VALID = 1704067200;  // 2024-01-01
static uint32_t g_lastRtcWrite = 0;
static bool g_ntpSeen = false;

static uint8_t bcd2(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t toBcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

void timeRestoreRtc() {
  if (time(nullptr) >= MIN_VALID) return;
  Wire.beginTransmission(ADDR_RTC);
  Wire.write((uint8_t)0);
  if (Wire.endTransmission() != 0) return;
  if (Wire.requestFrom(ADDR_RTC, (uint8_t)7) != 7) return;
  uint8_t r[7];
  for (int i = 0; i < 7; i++) r[i] = Wire.read();
  Wire.beginTransmission(ADDR_RTC);
  Wire.write((uint8_t)8);
  Wire.endTransmission();
  if (Wire.requestFrom(ADDR_RTC, (uint8_t)1) != 1) return;
  uint8_t marker = Wire.read();
  if (marker != 0xA5 || (r[0] & 0x80)) return;  // not written by us, or oscillator halted
  int y = 2000 + bcd2(r[6]), mo = bcd2(r[5]), d = bcd2(r[4]);
  int h = bcd2(r[2] & 0x3F), mi = bcd2(r[1]), s = bcd2(r[0] & 0x7F);
  time_t t = (time_t)(daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s);
  if (t < MIN_VALID) return;
  struct timeval tv = {t, 0};
  settimeofday(&tv, nullptr);
  log("Clock restored from RTC (UTC %04d-%02d-%02d %02d:%02d)", y, mo, d, h, mi);
}

static void rtcWrite(time_t t) {
  struct tm g;
  gmtime_r(&t, &g);
  Wire.beginTransmission(ADDR_RTC);
  Wire.write((uint8_t)0);
  Wire.write(toBcd(g.tm_sec));
  Wire.write(toBcd(g.tm_min));
  Wire.write(toBcd(g.tm_hour));
  Wire.write(toBcd(g.tm_wday + 1));
  Wire.write(toBcd(g.tm_mday));
  Wire.write(toBcd(g.tm_mon + 1));
  Wire.write(toBcd(g.tm_year % 100));
  Wire.write(0xA5);  // RAM byte at register 8: "holds UTC written by WaterFill"
  Wire.endTransmission();
}

void timeSetTz(const char* tz) {
  setenv("TZ", tz, 1);
  tzset();
}

void timeStartNtp(const char* tz) {
  configTzTime(tz, "pool.ntp.org", "time.google.com");
}

void timeService() {
  time_t t = time(nullptr);
  if (t < MIN_VALID) return;
  if (!g_ntpSeen && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
    g_ntpSeen = true;
    log("NTP synchronised");
    rtcWrite(t);
    g_lastRtcWrite = millis();
  } else if (g_ntpSeen && millis() - g_lastRtcWrite > 6UL * 3600 * 1000) {
    rtcWrite(t);
    g_lastRtcWrite = millis();
  }
}

bool timeValid() { return time(nullptr) >= MIN_VALID; }
uint32_t epoch() { time_t t = time(nullptr); return t >= MIN_VALID ? (uint32_t)t : 0; }

uint16_t localMinutes() {
  time_t t = time(nullptr);
  struct tm l;
  localtime_r(&t, &l);
  return (uint16_t)(l.tm_hour * 60 + l.tm_min);
}

uint16_t localDay() {
  time_t t = time(nullptr);
  struct tm l;
  localtime_r(&t, &l);
  return (uint16_t)daysFromCivil(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday);
}

}  // namespace plat
