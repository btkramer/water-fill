// MQTT + Home Assistant discovery. Runs in its own task: a slow or missing broker can never
// stall the control loop.
//
// Topics (base "waterfill"):  status (JSON, every 5 s and on change), availability (LWT),
// cmd (commands, see mqtt.cpp). HA entities are published under homeassistant/<type>/<node>/<id>/config
// with node = "waterfill_<last 3 MAC bytes>".
#pragma once
#include <Arduino.h>

namespace mqtt {

struct Cfg {
  bool en;
  char host[48];
  uint16_t port;
  char user[24];
  char pass[32];
};

void begin();
bool connected();
const char* node();
void getCfg(Cfg& c);
bool setCfg(const Cfg& c, bool keepPass);   // persists and applies (reconnects)

}  // namespace mqtt
