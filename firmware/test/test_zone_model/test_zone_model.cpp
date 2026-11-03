// Tests for the zone model: change detection and mute emulation (SPEC §4.3, §8).
#include <unity.h>

#include "rnet_messages.h"
#include "zone_model.h"

using rnet::ZoneInfo;
using rnet::ZoneModel;

void setUp() {}
void tearDown() {}

namespace {

ZoneInfo info(uint8_t zone, uint8_t volume) {
  ZoneInfo z{};
  z.zone = zone;
  z.power = true;
  z.source = 2;
  z.volume = volume;
  z.system_on = true;
  return z;
}

// A model with `zone` polled once at `volume`, and the change flags cleared.
ZoneModel polled(uint8_t zone, uint8_t volume) {
  ZoneModel m;
  m.apply(info(zone, volume));
  m.take_changed();
  return m;
}

void test_zones_start_invalid() {
  ZoneModel m;
  for (uint8_t z = 1; z <= rnet::kNumZones; ++z) {
    TEST_ASSERT_FALSE(m.zone(z).valid);
    TEST_ASSERT_FALSE(m.zone(z).turn_on_volume_valid);
  }
  TEST_ASSERT_EQUAL_HEX8(0, m.take_changed());
}

void test_first_poll_marks_valid_and_changed() {
  ZoneModel m;
  ZoneInfo z = info(3, 24);
  z.bass = -2;
  m.apply(z);
  TEST_ASSERT_TRUE(m.zone(3).valid);
  TEST_ASSERT_EQUAL_UINT8(24, m.zone(3).volume);
  TEST_ASSERT_EQUAL_INT8(-2, m.zone(3).bass);
  TEST_ASSERT_EQUAL_HEX8(1u << 2, m.take_changed());
  TEST_ASSERT_EQUAL_HEX8(0, m.take_changed());  // take clears
}

void test_identical_poll_is_not_a_change() {
  ZoneModel m = polled(1, 35);
  m.apply(info(1, 35));
  TEST_ASSERT_EQUAL_HEX8(0, m.take_changed());
}

void test_each_field_change_is_detected() {
  ZoneModel m = polled(6, 15);
  ZoneInfo z = info(6, 15);
  z.loudness = true;
  m.apply(z);
  TEST_ASSERT_EQUAL_HEX8(1u << 5, m.take_changed());
  z.source = 4;
  m.apply(z);
  TEST_ASSERT_EQUAL_HEX8(1u << 5, m.take_changed());
  z.shared_source = true;
  m.apply(z);
  TEST_ASSERT_EQUAL_HEX8(1u << 5, m.take_changed());
}

void test_invalid_zone_numbers_are_ignored() {
  ZoneModel m;
  m.apply(info(0, 10));
  m.apply(info(7, 10));
  m.apply_turn_on_volume(0, 10);
  uint8_t v = 0;
  TEST_ASSERT_FALSE(m.mute(7, v));
  TEST_ASSERT_FALSE(m.step_volume(0, 1, v));
  TEST_ASSERT_EQUAL_HEX8(0, m.take_changed());
}

void test_turn_on_volume() {
  ZoneModel m;
  m.apply_turn_on_volume(1, 35);
  TEST_ASSERT_TRUE(m.zone(1).turn_on_volume_valid);
  TEST_ASSERT_EQUAL_UINT8(35, m.zone(1).turn_on_volume);
  TEST_ASSERT_EQUAL_HEX8(1, m.take_changed());
  m.apply_turn_on_volume(1, 35);
  TEST_ASSERT_EQUAL_HEX8(0, m.take_changed());
  m.apply_turn_on_volume(1, 51);  // out of range: ignored
  TEST_ASSERT_EQUAL_UINT8(35, m.zone(1).turn_on_volume);
}

// --- Mute emulation ---

void test_mute_needs_a_polled_zone() {
  ZoneModel m;
  uint8_t v = 99;
  TEST_ASSERT_FALSE(m.mute(1, v));
  TEST_ASSERT_FALSE(m.zone(1).muted);
}

void test_mute_then_unmute_restores_volume() {
  ZoneModel m = polled(1, 30);
  uint8_t v = 99;
  TEST_ASSERT_TRUE(m.mute(1, v));
  TEST_ASSERT_EQUAL_UINT8(0, v);
  TEST_ASSERT_TRUE(m.zone(1).muted);
  TEST_ASSERT_EQUAL_HEX8(1, m.take_changed());

  m.apply(info(1, 0));  // the confirming poll keeps it muted
  TEST_ASSERT_TRUE(m.zone(1).muted);

  TEST_ASSERT_TRUE(m.unmute(1, v));
  TEST_ASSERT_EQUAL_UINT8(30, v);
  TEST_ASSERT_FALSE(m.zone(1).muted);
}

void test_unmute_from_zero_uses_default() {
  ZoneModel m = polled(2, 0);
  uint8_t v = 0;
  TEST_ASSERT_TRUE(m.mute(2, v));
  TEST_ASSERT_TRUE(m.unmute(2, v));
  TEST_ASSERT_EQUAL_UINT8(ZoneModel::kUnmuteDefaultVolume, v);
}

void test_unmute_when_not_muted_sends_nothing() {
  ZoneModel m = polled(1, 30);
  uint8_t v = 99;
  TEST_ASSERT_FALSE(m.unmute(1, v));
  TEST_ASSERT_EQUAL_UINT8(99, v);
}

void test_mute_while_muted_keeps_stored_volume() {
  ZoneModel m = polled(1, 30);
  uint8_t v = 0;
  TEST_ASSERT_TRUE(m.mute(1, v));
  m.apply(info(1, 0));
  TEST_ASSERT_TRUE(m.mute(1, v));  // second mute
  TEST_ASSERT_EQUAL_UINT8(0, v);
  TEST_ASSERT_TRUE(m.unmute(1, v));
  TEST_ASSERT_EQUAL_UINT8(30, v);
}

void test_keypad_volume_change_clears_mute() {
  ZoneModel m = polled(1, 30);
  uint8_t v = 0;
  TEST_ASSERT_TRUE(m.mute(1, v));
  m.apply(info(1, 0));
  m.take_changed();
  m.apply(info(1, 1));  // someone pressed volume up at the keypad
  TEST_ASSERT_FALSE(m.zone(1).muted);
  TEST_ASSERT_EQUAL_UINT8(1, m.zone(1).volume);
  TEST_ASSERT_EQUAL_HEX8(1, m.take_changed());
}

void test_mute_right_after_set_volume_stores_commanded_volume() {
  ZoneModel m = polled(1, 30);
  m.note_commanded_volume(1, 40);  // Set Volume 40 sent, not yet polled
  uint8_t v = 0;
  TEST_ASSERT_TRUE(m.mute(1, v));
  TEST_ASSERT_TRUE(m.unmute(1, v));
  TEST_ASSERT_EQUAL_UINT8(40, v);
}

// --- Volume steps ---

void test_step_needs_a_polled_zone() {
  ZoneModel m;
  uint8_t v = 0;
  TEST_ASSERT_FALSE(m.step_volume(1, 1, v));
}

void test_step_up_and_down() {
  ZoneModel m = polled(1, 30);
  uint8_t v = 0;
  TEST_ASSERT_TRUE(m.step_volume(1, 1, v));
  TEST_ASSERT_EQUAL_UINT8(31, v);
  m.apply(info(1, 31));
  TEST_ASSERT_TRUE(m.step_volume(1, -1, v));
  TEST_ASSERT_EQUAL_UINT8(30, v);
}

void test_step_clamps_at_limits() {
  ZoneModel m = polled(1, 50);
  uint8_t v = 0;
  TEST_ASSERT_TRUE(m.step_volume(1, 1, v));
  TEST_ASSERT_EQUAL_UINT8(50, v);
  m = polled(1, 0);
  TEST_ASSERT_TRUE(m.step_volume(1, -1, v));
  TEST_ASSERT_EQUAL_UINT8(0, v);
}

void test_repeated_steps_build_on_commanded_volume() {
  ZoneModel m = polled(1, 30);
  uint8_t v = 0;
  TEST_ASSERT_TRUE(m.step_volume(1, 1, v));
  m.note_commanded_volume(1, v);
  TEST_ASSERT_TRUE(m.step_volume(1, 1, v));  // before any poll confirms 31
  TEST_ASSERT_EQUAL_UINT8(32, v);
  m.note_commanded_volume(1, v);
  m.apply(info(1, 32));  // poll confirms; the base is the reported volume again
  TEST_ASSERT_TRUE(m.step_volume(1, -1, v));
  TEST_ASSERT_EQUAL_UINT8(31, v);
}

void test_step_while_muted_uses_stored_volume_and_unmutes() {
  ZoneModel m = polled(1, 30);
  uint8_t v = 0;
  TEST_ASSERT_TRUE(m.mute(1, v));
  m.apply(info(1, 0));
  m.take_changed();
  TEST_ASSERT_TRUE(m.step_volume(1, 1, v));
  TEST_ASSERT_EQUAL_UINT8(31, v);
  TEST_ASSERT_FALSE(m.zone(1).muted);
  TEST_ASSERT_EQUAL_HEX8(1, m.take_changed());

  m = polled(1, 30);
  TEST_ASSERT_TRUE(m.mute(1, v));
  TEST_ASSERT_TRUE(m.step_volume(1, -1, v));
  TEST_ASSERT_EQUAL_UINT8(29, v);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_zones_start_invalid);
  RUN_TEST(test_first_poll_marks_valid_and_changed);
  RUN_TEST(test_identical_poll_is_not_a_change);
  RUN_TEST(test_each_field_change_is_detected);
  RUN_TEST(test_invalid_zone_numbers_are_ignored);
  RUN_TEST(test_turn_on_volume);
  RUN_TEST(test_mute_needs_a_polled_zone);
  RUN_TEST(test_mute_then_unmute_restores_volume);
  RUN_TEST(test_unmute_from_zero_uses_default);
  RUN_TEST(test_unmute_when_not_muted_sends_nothing);
  RUN_TEST(test_mute_while_muted_keeps_stored_volume);
  RUN_TEST(test_keypad_volume_change_clears_mute);
  RUN_TEST(test_mute_right_after_set_volume_stores_commanded_volume);
  RUN_TEST(test_step_needs_a_polled_zone);
  RUN_TEST(test_step_up_and_down);
  RUN_TEST(test_step_clamps_at_limits);
  RUN_TEST(test_repeated_steps_build_on_commanded_volume);
  RUN_TEST(test_step_while_muted_uses_stored_volume_and_unmutes);
  return UNITY_END();
}
