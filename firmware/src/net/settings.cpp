#include "net/settings.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <cstring>

namespace {

constexpr const char* kNamespace = "rnet";

void copy(char* dst, size_t cap, const String& src) {
  std::strncpy(dst, src.c_str(), cap - 1);
  dst[cap - 1] = '\0';
}

// Preferences logs an error for a missing key; check first so a fresh board boots quietly.
String get(Preferences& p, const char* key, const char* fallback) {
  return p.isKey(key) ? p.getString(key, fallback) : String(fallback);
}

}  // namespace

void Settings::load(const char* default_hostname) {
  Preferences p;
  p.begin(kNamespace, true);
  copy(hostname, sizeof(hostname), get(p, "hostname", default_hostname));
  copy(token, sizeof(token), get(p, "token", ""));
  copy(ota_password, sizeof(ota_password), get(p, "ota_pw", ""));
  p.end();
  if (hostname[0] == '\0') {
    copy(hostname, sizeof(hostname), String(default_hostname));
  }
}

void Settings::save() const {
  Preferences p;
  p.begin(kNamespace, false);
  p.putString("hostname", hostname);
  p.putString("token", token);
  p.putString("ota_pw", ota_password);
  p.end();
}

void Settings::factory_reset() {
  Preferences p;
  p.begin(kNamespace, false);
  p.clear();
  p.end();
  // The Wi-Fi driver is off while on Ethernet, and erasing needs it started.
  WiFi.mode(WIFI_STA);
  esp_wifi_restore();  // erases the stored Wi-Fi configuration
}

bool Settings::double_reset_flag() {
  Preferences p;
  p.begin(kNamespace, true);
  const bool on = p.isKey("dr") && p.getBool("dr", false);
  p.end();
  return on;
}

void Settings::set_double_reset_flag(bool on) {
  Preferences p;
  p.begin(kNamespace, false);
  p.putBool("dr", on);
  p.end();
}
