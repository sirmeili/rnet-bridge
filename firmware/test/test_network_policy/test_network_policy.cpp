// Tests for the Ethernet-first / Wi-Fi-fallback rules (SPEC §4.4) and double-reset detection.
#include <unity.h>

#include "double_reset.h"
#include "network_policy.h"

using bridge::DoubleResetDetector;
using bridge::Iface;
using bridge::NetInputs;
using bridge::NetPlan;
using bridge::NetworkPolicy;

void setUp() {}
void tearDown() {}

namespace {

constexpr NetInputs kNothing{false, false, true};      // no link yet, Wi-Fi credentials saved
constexpr NetInputs kEth{true, false, true};
constexpr NetInputs kWifiUp{false, true, true};
constexpr NetInputs kNoCreds{false, false, false};

void assert_plan(const NetPlan& p, bool wifi, bool portal, Iface serving) {
  TEST_ASSERT_EQUAL_MESSAGE(wifi, p.wifi_sta, "wifi_sta");
  TEST_ASSERT_EQUAL_MESSAGE(portal, p.portal, "portal");
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(serving), static_cast<int>(p.serving), "serving");
}

void test_ethernet_at_boot_serves_on_ethernet() {
  NetworkPolicy p(false);
  assert_plan(p.update(0, kNothing), false, false, Iface::kNone);
  assert_plan(p.update(2000, kEth), false, false, Iface::kEthernet);
  TEST_ASSERT_TRUE(p.take_serving_changed());
  TEST_ASSERT_FALSE(p.take_serving_changed());
}

void test_wifi_waits_for_the_ethernet_boot_window() {
  NetworkPolicy p(false);
  assert_plan(p.update(0, kNothing), false, false, Iface::kNone);
  assert_plan(p.update(9999, kNothing), false, false, Iface::kNone);
  assert_plan(p.update(10000, kNothing), true, false, Iface::kNone);
  assert_plan(p.update(12000, kWifiUp), true, false, Iface::kWifi);
}

void test_ethernet_returning_takes_over_from_wifi() {
  NetworkPolicy p(false);
  p.update(0, kNothing);
  p.update(10000, kWifiUp);
  p.take_serving_changed();
  assert_plan(p.update(20000, NetInputs{true, true, true}), false, false, Iface::kEthernet);
  TEST_ASSERT_TRUE(p.take_serving_changed());
}

void test_ethernet_dropping_falls_back_to_wifi_at_once() {
  NetworkPolicy p(false);
  p.update(0, kEth);
  // After boot there is no 10 s wait: Wi-Fi starts as soon as Ethernet is gone.
  assert_plan(p.update(60000, kNothing), true, false, Iface::kNone);
  assert_plan(p.update(61000, kWifiUp), true, false, Iface::kWifi);
}

void test_no_credentials_and_no_ethernet_opens_portal() {
  NetworkPolicy p(false);
  p.update(0, kNoCreds);
  assert_plan(p.update(10000, kNoCreds), false, true, Iface::kNone);
}

void test_ethernet_without_credentials_does_not_open_portal() {
  NetworkPolicy p(false);
  assert_plan(p.update(0, NetInputs{true, false, false}), false, false, Iface::kEthernet);
  assert_plan(p.update(100000, NetInputs{true, false, false}), false, false, Iface::kEthernet);
}

void test_wifi_failing_for_60_s_opens_portal_and_keeps_trying() {
  NetworkPolicy p(false);
  p.update(0, kNothing);
  assert_plan(p.update(10000, kNothing), true, false, Iface::kNone);
  assert_plan(p.update(69999, kNothing), true, false, Iface::kNone);
  assert_plan(p.update(70000, kNothing), true, true, Iface::kNone);
  // The saved network comes back: portal closes.
  assert_plan(p.update(80000, kWifiUp), true, false, Iface::kWifi);
}

void test_wifi_fail_timer_restarts_after_a_connection() {
  NetworkPolicy p(false);
  p.update(0, kNothing);
  p.update(10000, kNothing);
  p.update(50000, kWifiUp);
  // Dropped again at 60 s: a fresh 60 s before the portal.
  assert_plan(p.update(60000, kNothing), true, false, Iface::kNone);
  assert_plan(p.update(119999, kNothing), true, false, Iface::kNone);
  assert_plan(p.update(120000, kNothing), true, true, Iface::kNone);
}

void test_forced_portal_runs_alongside_ethernet() {
  NetworkPolicy p(true);
  assert_plan(p.update(0, kEth), false, true, Iface::kEthernet);
  p.portal_done();
  assert_plan(p.update(1000, kEth), false, false, Iface::kEthernet);
}

void test_forced_portal_times_out() {
  NetworkPolicy p(true);
  assert_plan(p.update(0, kEth), false, true, Iface::kEthernet);
  assert_plan(p.update(299999, kEth), false, true, Iface::kEthernet);
  assert_plan(p.update(300000, kEth), false, false, Iface::kEthernet);
}

void test_timers_survive_millis_wrap() {
  NetworkPolicy p(false);
  const uint32_t start = 0xFFFFF000u;  // ~4 s before millis() wraps
  p.update(start, kNothing);
  assert_plan(p.update(start + 9999, kNothing), false, false, Iface::kNone);
  assert_plan(p.update(start + 10000, kNothing), true, false, Iface::kNone);
}

// --- Double reset ---

void test_single_boot_is_not_a_double_reset() {
  DoubleResetDetector d;
  TEST_ASSERT_FALSE(d.begin(false, 100));
  TEST_ASSERT_FALSE(d.should_clear(3099));
  TEST_ASSERT_TRUE(d.should_clear(3100));
  TEST_ASSERT_FALSE(d.should_clear(9000));  // only once
}

void test_flag_still_set_at_boot_is_a_double_reset() {
  DoubleResetDetector d;
  TEST_ASSERT_TRUE(d.begin(true, 50));
  TEST_ASSERT_TRUE(d.should_clear(3050));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ethernet_at_boot_serves_on_ethernet);
  RUN_TEST(test_wifi_waits_for_the_ethernet_boot_window);
  RUN_TEST(test_ethernet_returning_takes_over_from_wifi);
  RUN_TEST(test_ethernet_dropping_falls_back_to_wifi_at_once);
  RUN_TEST(test_no_credentials_and_no_ethernet_opens_portal);
  RUN_TEST(test_ethernet_without_credentials_does_not_open_portal);
  RUN_TEST(test_wifi_failing_for_60_s_opens_portal_and_keeps_trying);
  RUN_TEST(test_wifi_fail_timer_restarts_after_a_connection);
  RUN_TEST(test_forced_portal_runs_alongside_ethernet);
  RUN_TEST(test_forced_portal_times_out);
  RUN_TEST(test_timers_survive_millis_wrap);
  RUN_TEST(test_single_boot_is_not_a_double_reset);
  RUN_TEST(test_flag_still_set_at_boot_is_a_double_reset);
  return UNITY_END();
}
