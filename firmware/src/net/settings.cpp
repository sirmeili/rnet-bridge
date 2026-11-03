#include "net/settings.h"

#include <Preferences.h>
#include <WiFi.h>

#include <cstring>

namespace {

constexpr const char* kNamespace = "rnet";

void copy(char* dst, size_t cap, const String& src) {
  std::strncpy(dst, src.c_str(), cap - 1);
  dst[cap - 1] = '\0';
}

}  // namespace

void Settings::load(const char* default_hostname) {
  Preferences p;
  p.begin(kNamespace, true);
  copy(hostname, sizeof(hostname), p.getString("hostname", default_hostname));
  copy(token, sizeof(token), p.getString("token", ""));
  copy(ota_password, sizeof(ota_password), p.getString("ota_pw", ""));
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
  WiFi.disconnect(true, true);  // also erases the stored Wi-Fi credentials
}

bool Settings::double_reset_flag() {
  Preferences p;
  p.begin(kNamespace, true);
  const bool on = p.getBool("dr", false);
  p.end();
  return on;
}

void Settings::set_double_reset_flag(bool on) {
  Preferences p;
  p.begin(kNamespace, false);
  p.putBool("dr", on);
  p.end();
}
