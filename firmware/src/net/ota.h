// ArduinoOTA for development uploads (`pio run -t upload --upload-port <host>`, SPEC §4.6). The
// release path is POST /api/ota in api_server.
#pragma once

namespace ota {

// Starts ArduinoOTA once the bridge has an address. Its own mDNS is off: mdns.cpp owns mDNS.
void begin(const char* hostname, const char* password);
void loop();

}  // namespace ota
