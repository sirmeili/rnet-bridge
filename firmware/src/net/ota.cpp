#include "net/ota.h"

#include <ArduinoOTA.h>

#include "platform/log.h"

namespace ota {

namespace {
bool g_started = false;
}  // namespace

void begin(const char* hostname, const char* password) {
  if (g_started) {
    return;
  }
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.setMdnsEnabled(false);
  if (password[0] != '\0') {
    ArduinoOTA.setPassword(password);
  }
  ArduinoOTA.onStart([]() { LOG_WARN("ota: ArduinoOTA upload started"); });
  ArduinoOTA.onError([](ota_error_t e) { LOG_ERROR("ota: ArduinoOTA error %d", static_cast<int>(e)); });
  ArduinoOTA.begin();
  g_started = true;
}

void loop() {
  if (g_started) {
    ArduinoOTA.handle();
  }
}

}  // namespace ota
