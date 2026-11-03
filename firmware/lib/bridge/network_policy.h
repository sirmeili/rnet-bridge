// Ethernet-first, Wi-Fi-fallback decisions (SPEC §4.4), separated from the radio and PHY drivers so
// the rules can be tested on the host. The device code reports what it sees each tick and applies
// the returned plan. Pure C++17, no Arduino dependencies.
#pragma once

#include <cstdint>

namespace bridge {

enum class Iface : uint8_t { kNone, kEthernet, kWifi };

struct NetworkPolicyConfig {
  uint32_t ethernet_boot_wait_ms = 10000;  // at boot, how long Ethernet gets before Wi-Fi is tried
  uint32_t wifi_fail_ms = 60000;           // Wi-Fi not connecting this long starts the portal
  uint32_t forced_portal_ms = 300000;      // a portal forced by double reset closes after this
};

struct NetInputs {
  bool eth_has_ip;
  bool wifi_connected;
  bool wifi_has_credentials;
};

struct NetPlan {
  bool wifi_sta;  // Wi-Fi station should be on (connecting or connected)
  bool portal;    // the setup portal (AP "RNET-Bridge-XXXX") should be running
  Iface serving;  // where the API is reachable now
};

class NetworkPolicy {
 public:
  // `force_portal` is true after a double reset: open the portal even when Ethernet is up.
  explicit NetworkPolicy(bool force_portal, const NetworkPolicyConfig& config = NetworkPolicyConfig{});

  NetPlan update(uint32_t now_ms, const NetInputs& in);

  // The portal finished (credentials saved or the user closed it): stop forcing it.
  void portal_done() { forced_ = false; }

  // True once since the last call if NetPlan::serving changed (mDNS re-announces on this).
  bool take_serving_changed();

 private:
  NetworkPolicyConfig cfg_;
  bool forced_;
  bool started_ = false;
  uint32_t start_ms_ = 0;
  bool boot_wait_over_ = false;
  bool wifi_failing_ = false;
  uint32_t wifi_fail_since_ms_ = 0;
  Iface serving_ = Iface::kNone;
  bool serving_changed_ = false;
};

}  // namespace bridge
