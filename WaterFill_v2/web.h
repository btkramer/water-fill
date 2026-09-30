#pragma once

namespace web {
void begin();   // register routes and start the server (call once, after WiFi is up)
void tick();    // call from loop(): pushes live state to connected browsers
}
