// Bridge settings in NVS (SPEC §4.4): hostname, API token and OTA password. Wi-Fi credentials are
// kept by the Wi-Fi driver itself (set through the WiFiManager portal). Firmware stays config-free
// apart from these network settings (CLAUDE.md rule 4).
#pragma once

#include <Arduino.h>

#include "api_protocol.h"

struct Settings {
  char hostname[33];
  char token[bridge::kMaxTokenLen + 1];         // empty = no WebSocket auth
  char ota_password[bridge::kMaxTokenLen + 1];  // empty = OTA open

  // Loads from NVS. `default_hostname` is used when none is stored.
  void load(const char* default_hostname);
  void save() const;
  // Erases these settings and the Wi-Fi credentials.
  static void factory_reset();

  // Double-reset flag (SPEC §4.4).
  static bool double_reset_flag();
  static void set_double_reset_flag(bool on);
};
