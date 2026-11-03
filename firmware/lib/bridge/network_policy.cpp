#include "network_policy.h"

namespace bridge {

NetworkPolicy::NetworkPolicy(bool force_portal, const NetworkPolicyConfig& config)
    : cfg_(config), forced_(force_portal) {}

NetPlan NetworkPolicy::update(uint32_t now_ms, const NetInputs& in) {
  if (!started_) {
    started_ = true;
    start_ms_ = now_ms;
  }
  if (forced_ && now_ms - start_ms_ >= cfg_.forced_portal_ms) {
    forced_ = false;
  }

  NetPlan plan{false, forced_, Iface::kNone};
  if (in.eth_has_ip) {
    // Ethernet wins whenever it has an address. Wi-Fi is dropped (SPEC §4.4).
    boot_wait_over_ = true;
    wifi_failing_ = false;
    plan.serving = Iface::kEthernet;
  } else if (!boot_wait_over_ && now_ms - start_ms_ < cfg_.ethernet_boot_wait_ms) {
    // Boot: give Ethernet its chance before touching Wi-Fi.
  } else {
    boot_wait_over_ = true;
    if (!in.wifi_has_credentials) {
      plan.portal = true;
      wifi_failing_ = false;
    } else {
      plan.wifi_sta = true;
      if (in.wifi_connected) {
        wifi_failing_ = false;
        plan.serving = Iface::kWifi;
      } else {
        if (!wifi_failing_) {
          wifi_failing_ = true;
          wifi_fail_since_ms_ = now_ms;
        }
        if (now_ms - wifi_fail_since_ms_ >= cfg_.wifi_fail_ms) {
          plan.portal = true;  // keep trying the saved network while the portal is up
        }
      }
    }
  }

  if (plan.serving != serving_) {
    serving_ = plan.serving;
    serving_changed_ = true;
  }
  return plan;
}

bool NetworkPolicy::take_serving_changed() {
  const bool c = serving_changed_;
  serving_changed_ = false;
  return c;
}

}  // namespace bridge
