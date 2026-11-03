// Ethernet first, Wi-Fi fallback, setup portal (SPEC §4.4). Drives the LAN8720 PHY, the Wi-Fi
// station and the WiFiManager portal from bridge::NetworkPolicy's plan. Non-blocking after begin().
//
// The portal's web server needs port 80, which the API server also uses, so the API is paused
// while the portal is open (`portal_active()`). That only happens with no network at all, or for
// up to 5 minutes after a double reset.
#pragma once

#include <Arduino.h>

#include "net/settings.h"
#include "network_policy.h"

class Network {
 public:
  explicit Network(Settings& settings) : settings_(settings) {}

  // Starts Ethernet. `force_portal` after a double reset. Blocks briefly (PHY power-up).
  void begin(bool force_portal);
  void loop(uint32_t now_ms);

  bridge::Iface serving() const { return serving_; }
  bool take_serving_changed() { return policy_ != nullptr && policy_->take_serving_changed(); }
  bool portal_active() const { return portal_running_; }
  // True once after the portal saved new settings: the caller restarts the board.
  bool take_restart_requested();
  int wifi_rssi() const;
  String local_ip() const;
  // One diagnostic line for the USB `net` command.
  void print_status(Print& out) const;

  // Lowercase hex Ethernet MAC (stable bridge id, docs/protocol.md) and the last 4 hex digits.
  // Available before begin().
  static const char* id();
  static const char* id_suffix();

 private:
  void apply(const bridge::NetPlan& plan);
  void start_portal();
  void stop_portal();

  Settings& settings_;
  bridge::NetworkPolicy* policy_ = nullptr;
  bridge::Iface serving_ = bridge::Iface::kNone;
  bool sta_on_ = false;
  bool portal_running_ = false;
  bool has_credentials_ = false;
  uint32_t eth_events_seen_ = 0;
};
