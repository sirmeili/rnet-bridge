// Message layer tests: §3.3 builders against the §3.10 vectors, §3.4 reply parsers, and
// 1-based API / 0-based wire conversion.
#include <unity.h>

#include "../vectors.h"
#include "rnet_codec.h"
#include "rnet_messages.h"

using rnet::FrameParser;
using rnet::ParseResult;
using rnet::ReplyResult;
using rnet::WireFrame;
using rnet::ZoneParam;

void setUp() {}
void tearDown() {}

namespace {

// Reply frames with checksums computed independently (Python), not by our builder.
// All zone info, zone 3: on, source 2, volume 24, bass flat, treble -2, loudness off, balance
// center, system on.
constexpr uint8_t kZone3Info[] = {0xF0, 0x00, 0x02, 0x70, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x04, 0x02,
                                  0x00, 0x02, 0x07, 0x00, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x01, 0x01,
                                  0x18, 0x0A, 0x08, 0x00, 0x0A, 0x01, 0x00, 0x00, 0x00, 0x00, 0x54,
                                  0xF7};
// All zone info, zone 6: off, source 6, volume 50, bass +10, treble -10, loudness on, balance full
// left, system off, shared source.
constexpr uint8_t kZone6Info[] = {0xF0, 0x00, 0x05, 0x70, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x04, 0x02,
                                  0x00, 0x05, 0x07, 0x00, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x00, 0x05,
                                  0x32, 0x14, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x70,
                                  0xF7};
// Turn-on volume reply, zone 1 = 0x14, in the expected (not PDF) path format (SPEC §3.4, §3.9).
constexpr uint8_t kZone1TurnOnVolume[] = {0xF0, 0x00, 0x00, 0x70, 0x00, 0x00, 0x7F, 0x00,
                                          0x00, 0x05, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00,
                                          0x00, 0x01, 0x00, 0x01, 0x00, 0x14, 0x16, 0xF7};

const rnet::Frame& parse(FrameParser& p, const uint8_t* bytes, size_t len) {
  TEST_ASSERT_EQUAL(ParseResult::kFrame, feed_all(p, bytes, len));
  return p.frame();
}

// --- SPEC §3.10 vectors through the §3.3 builders ---

void test_vector_zone1_volume_up() {
  WireFrame out{};
  TEST_ASSERT_TRUE(rnet::build_keypad_event(1, rnet::event_id::kVolumeUp, out));
  assert_wire_equals(kZone1VolumeUp, sizeof(kZone1VolumeUp), out, "Zone 1 Volume Up");
}

void test_vector_zone1_volume_down() {
  WireFrame out{};
  TEST_ASSERT_TRUE(rnet::build_keypad_event(1, rnet::event_id::kVolumeDown, out));
  assert_wire_equals(kZone1VolumeDown, sizeof(kZone1VolumeDown), out, "Zone 1 Volume Down");
}

void test_vector_zone_power() {
  WireFrame out{};
  TEST_ASSERT_TRUE(rnet::build_zone_power(1, true, out));
  assert_wire_equals(kZone1On, sizeof(kZone1On), out, "Zone 1 On");
  TEST_ASSERT_TRUE(rnet::build_zone_power(1, false, out));
  assert_wire_equals(kZone1Off, sizeof(kZone1Off), out, "Zone 1 Off");
  TEST_ASSERT_TRUE(rnet::build_zone_power(2, true, out));
  assert_wire_equals(kZone2On, sizeof(kZone2On), out, "Zone 2 On");
}

void test_vector_all_zones() {
  WireFrame out{};
  TEST_ASSERT_TRUE(rnet::build_all_zones_power(true, out));
  assert_wire_equals(kAllOn, sizeof(kAllOn), out, "All On");
  TEST_ASSERT_TRUE(rnet::build_all_zones_power(false, out));
  assert_wire_equals(kAllOff, sizeof(kAllOff), out, "All Off");
}

void test_vector_select_source() {
  WireFrame out{};
  TEST_ASSERT_TRUE(rnet::build_select_source(1, 1, out));
  assert_wire_equals(kZone1Source1, sizeof(kZone1Source1), out, "Zone 1 Source 1");
  TEST_ASSERT_TRUE(rnet::build_select_source(6, 6, out));
  assert_wire_equals(kZone6Source6, sizeof(kZone6Source6), out, "Zone 6 Source 6");
}

void test_vector_zone1_menu() {
  WireFrame out{};
  TEST_ASSERT_TRUE(rnet::build_remote_key(1, 0x20, out));
  assert_wire_equals(kZone1Menu, sizeof(kZone1Menu), out, "Zone 1 Menu");
}

void test_vector_event_handshake() {
  WireFrame out{};
  TEST_ASSERT_TRUE(rnet::build_handshake({0x00, 0x00, 0x60}, {0x00, 0x7D, 0x00}, 0x06, out));
  assert_wire_equals(kEventHandshake, sizeof(kEventHandshake), out, "Event handshake");
}

// --- Other §3.3 templates ---

void test_set_volume_template() {
  // F0 00 00 7F 00 zz 70 05 02 02 00 00 F1 21 00 vv 00 zz 00 01, zone 3, volume 0x19.
  const uint8_t expected_body[] = {0x05, 0x02, 0x02, 0x00, 0x00, 0xF1, 0x21,
                                   0x00, 0x19, 0x00, 0x02, 0x00, 0x01};
  WireFrame out{};
  TEST_ASSERT_TRUE(rnet::build_set_volume(3, 25, out));
  TEST_ASSERT_EQUAL_size_t(22, out.len);
  TEST_ASSERT_EQUAL_HEX8(0x02, out.bytes[5]);  // source zone = zz
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_body, out.bytes + 7, sizeof(expected_body));
  TEST_ASSERT_EQUAL_HEX8(rnet::checksum(out.bytes, 20), out.bytes[20]);
}

void test_set_zone_param_template() {
  // F0 00 00 7F 00 zz 70 00 05 02 00 zz 00 pp 00 00 00 01 00 01 00 vv, zone 2, treble -2 (0x08).
  const uint8_t expected[] = {0xF0, 0x00, 0x00, 0x7F, 0x00, 0x01, 0x70, 0x00, 0x05, 0x02, 0x00,
                              0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x08};
  WireFrame out{};
  TEST_ASSERT_TRUE(rnet::build_set_zone_param(2, ZoneParam::kTreble, -2, out));
  TEST_ASSERT_EQUAL_size_t(sizeof(expected) + 2, out.len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out.bytes, sizeof(expected));
  TEST_ASSERT_EQUAL_HEX8(rnet::checksum(expected, sizeof(expected)), out.bytes[sizeof(expected)]);
}

void test_set_zone_param_encodings() {
  WireFrame out{};
  TEST_ASSERT_TRUE(rnet::build_set_zone_param(1, ZoneParam::kBass, -10, out));
  TEST_ASSERT_EQUAL_HEX8(0x00, out.bytes[21]);
  TEST_ASSERT_TRUE(rnet::build_set_zone_param(1, ZoneParam::kBalance, 10, out));
  TEST_ASSERT_EQUAL_HEX8(0x14, out.bytes[21]);
  TEST_ASSERT_TRUE(rnet::build_set_zone_param(1, ZoneParam::kLoudness, 1, out));
  TEST_ASSERT_EQUAL_HEX8(0x01, out.bytes[21]);
  TEST_ASSERT_TRUE(rnet::build_set_zone_param(1, ZoneParam::kTurnOnVolume, 50, out));
  TEST_ASSERT_EQUAL_HEX8(0x32, out.bytes[21]);
}

void test_get_zone_param_template() {
  // F0 00 00 7F 00 zz 70 01 05 02 00 zz 00 pp 00 00, zone 4, turn-on volume.
  const uint8_t expected[] = {0xF0, 0x00, 0x00, 0x7F, 0x00, 0x03, 0x70, 0x01,
                              0x05, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00};
  WireFrame out{};
  TEST_ASSERT_TRUE(rnet::build_get_zone_param(4, ZoneParam::kTurnOnVolume, out));
  TEST_ASSERT_EQUAL_size_t(sizeof(expected) + 2, out.len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out.bytes, sizeof(expected));
}

void test_get_all_zone_info_template() {
  // F0 00 00 7F 00 zz 70 01 04 02 00 zz 07 00 00, zone 6.
  const uint8_t expected[] = {0xF0, 0x00, 0x00, 0x7F, 0x00, 0x05, 0x70, 0x01,
                              0x04, 0x02, 0x00, 0x05, 0x07, 0x00, 0x00};
  WireFrame out{};
  TEST_ASSERT_TRUE(rnet::build_get_all_zone_info(6, out));
  TEST_ASSERT_EQUAL_size_t(sizeof(expected) + 2, out.len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out.bytes, sizeof(expected));
  TEST_ASSERT_EQUAL_HEX8(rnet::kEndOfMessage, out.bytes[out.len - 1]);
}

void test_session_start_device_restart() {
  // SPEC §3.6 with keypad 70: checksum 0x0A (computed independently).
  const uint8_t expected[] = {0xF0, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x70, 0x05, 0x02, 0x02, 0x00,
                              0x00, 0x55, 0x01, 0x03, 0x00, 0x35, 0x00, 0x01, 0x0A, 0xF7};
  WireFrame out{};
  TEST_ASSERT_TRUE(rnet::build_session_start(rnet::SessionStartEvent::kDeviceRestart, out));
  assert_wire_equals(expected, sizeof(expected), out, "Device Restart");
  TEST_ASSERT_FALSE(rnet::build_session_start(rnet::SessionStartEvent::kNone, out));
}

void test_data_handshake_hypothesis() {
  // SPEC §3.5 working hypothesis: F0 00 00 7F 00 zz 70 02 02 cs F7. Type byte is a parameter.
  WireFrame out{};
  TEST_ASSERT_TRUE(rnet::build_data_handshake(2, 0x02, out));
  const uint8_t expected[] = {0xF0, 0x00, 0x00, 0x7F, 0x00, 0x01, 0x70, 0x02, 0x02};
  TEST_ASSERT_EQUAL_size_t(sizeof(expected) + 2, out.len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out.bytes, sizeof(expected));
  TEST_ASSERT_TRUE(rnet::build_data_handshake(2, 0x06, out));
  TEST_ASSERT_EQUAL_HEX8(0x06, out.bytes[8]);
}

void test_builders_reject_invalid_arguments() {
  WireFrame out{};
  TEST_ASSERT_FALSE(rnet::build_zone_power(0, true, out));
  TEST_ASSERT_EQUAL_size_t(0, out.len);
  TEST_ASSERT_FALSE(rnet::build_zone_power(7, true, out));
  TEST_ASSERT_FALSE(rnet::build_select_source(1, 0, out));
  TEST_ASSERT_FALSE(rnet::build_select_source(1, 7, out));
  TEST_ASSERT_FALSE(rnet::build_set_volume(1, 51, out));
  TEST_ASSERT_FALSE(rnet::build_set_zone_param(1, ZoneParam::kBass, 11, out));
  TEST_ASSERT_FALSE(rnet::build_set_zone_param(1, ZoneParam::kTreble, -11, out));
  TEST_ASSERT_FALSE(rnet::build_set_zone_param(1, ZoneParam::kLoudness, 2, out));
  TEST_ASSERT_FALSE(rnet::build_set_zone_param(1, ZoneParam::kTurnOnVolume, 51, out));
  TEST_ASSERT_FALSE(rnet::build_set_zone_param(1, ZoneParam::kTurnOnVolume, -1, out));
  TEST_ASSERT_FALSE(rnet::build_get_all_zone_info(0, out));
  TEST_ASSERT_FALSE(rnet::build_data_handshake(7, 0x02, out));
}

// --- SPEC §3.4 reply parsers ---

void test_parse_all_zone_info_zone3() {
  FrameParser p;
  rnet::ZoneInfo info{};
  TEST_ASSERT_EQUAL(ReplyResult::kOk,
                    rnet::parse_all_zone_info(parse(p, kZone3Info, sizeof(kZone3Info)), info));
  TEST_ASSERT_EQUAL_UINT8(3, info.zone);
  TEST_ASSERT_TRUE(info.power);
  TEST_ASSERT_EQUAL_UINT8(2, info.source);
  TEST_ASSERT_EQUAL_UINT8(24, info.volume);
  TEST_ASSERT_EQUAL_INT8(0, info.bass);
  TEST_ASSERT_EQUAL_INT8(-2, info.treble);
  TEST_ASSERT_FALSE(info.loudness);
  TEST_ASSERT_EQUAL_INT8(0, info.balance);
  TEST_ASSERT_TRUE(info.system_on);
  TEST_ASSERT_FALSE(info.shared_source);
}

void test_parse_all_zone_info_zone6_extremes() {
  FrameParser p;
  rnet::ZoneInfo info{};
  TEST_ASSERT_EQUAL(ReplyResult::kOk,
                    rnet::parse_all_zone_info(parse(p, kZone6Info, sizeof(kZone6Info)), info));
  TEST_ASSERT_EQUAL_UINT8(6, info.zone);
  TEST_ASSERT_FALSE(info.power);
  TEST_ASSERT_EQUAL_UINT8(6, info.source);
  TEST_ASSERT_EQUAL_UINT8(50, info.volume);
  TEST_ASSERT_EQUAL_INT8(10, info.bass);
  TEST_ASSERT_EQUAL_INT8(-10, info.treble);
  TEST_ASSERT_TRUE(info.loudness);
  TEST_ASSERT_EQUAL_INT8(-10, info.balance);
  TEST_ASSERT_FALSE(info.system_on);
  TEST_ASSERT_TRUE(info.shared_source);
}

void test_parse_all_zone_info_uses_path_not_length() {
  // Same reply with two extra trailing data bytes and a larger length field: still parses.
  uint8_t body[] = {0x00, 0x04, 0x02, 0x00, 0x02, 0x07, 0x00, 0x00, 0x01, 0x00,
                    0x0E, 0x00, 0x01, 0x01, 0x18, 0x0A, 0x08, 0x00, 0x0A, 0x01,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  WireFrame out{};
  rnet::FrameBuilder b({0x00, 0x02, 0x70}, rnet::kController, 0x00);
  TEST_ASSERT_TRUE(b.put(body, sizeof(body)).finish(out));
  FrameParser p;
  rnet::ZoneInfo info{};
  TEST_ASSERT_EQUAL(ReplyResult::kOk, rnet::parse_all_zone_info(parse(p, out.bytes, out.len), info));
  TEST_ASSERT_EQUAL_UINT8(3, info.zone);
  TEST_ASSERT_EQUAL_UINT8(24, info.volume);
}

void test_parse_all_zone_info_rejects_bad_data() {
  // Truncated data (length field says 12, only 5 bytes present).
  const uint8_t truncated[] = {0x00, 0x04, 0x02, 0x00, 0x02, 0x07, 0x00, 0x00, 0x01,
                               0x00, 0x0C, 0x00, 0x01, 0x01, 0x18, 0x0A, 0x08};
  // Volume 0x33 (> 50).
  const uint8_t bad_volume[] = {0x00, 0x04, 0x02, 0x00, 0x02, 0x07, 0x00, 0x00, 0x01, 0x00,
                                0x0C, 0x00, 0x01, 0x01, 0x33, 0x0A, 0x08, 0x00, 0x0A, 0x01,
                                0x00, 0x00, 0x00, 0x00};
  // Zone 7 in the path.
  const uint8_t bad_zone[] = {0x00, 0x04, 0x02, 0x00, 0x06, 0x07, 0x00, 0x00, 0x01, 0x00,
                              0x0C, 0x00, 0x01, 0x01, 0x18, 0x0A, 0x08, 0x00, 0x0A, 0x01,
                              0x00, 0x00, 0x00, 0x00};
  struct Case {
    const uint8_t* body;
    size_t len;
    ReplyResult expected;
  };
  const Case cases[] = {
      {truncated, sizeof(truncated), ReplyResult::kNotThisMessage},  // data_len exceeds body
      {bad_volume, sizeof(bad_volume), ReplyResult::kInvalid},
      {bad_zone, sizeof(bad_zone), ReplyResult::kInvalid},
  };
  for (const Case& c : cases) {
    WireFrame out{};
    rnet::FrameBuilder b({0x00, 0x02, 0x70}, rnet::kController, 0x00);
    TEST_ASSERT_TRUE(b.put(c.body, c.len).finish(out));
    FrameParser p;
    rnet::ZoneInfo info{};
    TEST_ASSERT_EQUAL(c.expected, rnet::parse_all_zone_info(parse(p, out.bytes, out.len), info));
  }
}

void test_parse_all_zone_info_ignores_other_messages() {
  rnet::ZoneInfo info{};
  rnet::ZoneParamValue param{};
  for (const Vector& v : kFrameVectors) {
    FrameParser p;
    const rnet::Frame& f = parse(p, v.bytes, v.len);
    TEST_ASSERT_EQUAL_MESSAGE(ReplyResult::kNotThisMessage, rnet::parse_all_zone_info(f, info),
                              v.name);
    TEST_ASSERT_EQUAL_MESSAGE(ReplyResult::kNotThisMessage, rnet::parse_zone_param(f, param),
                              v.name);
  }
  FrameParser p;
  TEST_ASSERT_EQUAL(ReplyResult::kNotThisMessage,
                    rnet::parse_zone_param(parse(p, kZone3Info, sizeof(kZone3Info)), param));
}

void test_parse_turn_on_volume_reply() {
  FrameParser p;
  rnet::ZoneParamValue v{};
  TEST_ASSERT_EQUAL(ReplyResult::kOk,
                    rnet::parse_zone_param(
                        parse(p, kZone1TurnOnVolume, sizeof(kZone1TurnOnVolume)), v));
  TEST_ASSERT_EQUAL_UINT8(1, v.zone);
  TEST_ASSERT_EQUAL(ZoneParam::kTurnOnVolume, v.param);
  TEST_ASSERT_EQUAL_INT8(20, v.value);
  rnet::ZoneInfo info{};
  TEST_ASSERT_EQUAL(ReplyResult::kNotThisMessage, rnet::parse_all_zone_info(p.frame(), info));
}

void test_parse_zone_param_signed_values() {
  // Build replies for each tone param via the reply format and check signed conversion.
  struct Case {
    ZoneParam param;
    uint8_t wire;
    int8_t expected;
  };
  const Case cases[] = {
      {ZoneParam::kBass, 0x00, -10},
      {ZoneParam::kTreble, 0x0A, 0},
      {ZoneParam::kBalance, 0x14, 10},
      {ZoneParam::kLoudness, 0x01, 1},
  };
  for (const Case& c : cases) {
    const uint8_t body[] = {0x00, 0x05, 0x02, 0x00, 0x04, 0x00, static_cast<uint8_t>(c.param),
                            0x00, 0x00, 0x01, 0x00, 0x01, 0x00, c.wire};
    WireFrame out{};
    rnet::FrameBuilder b({0x00, 0x04, 0x70}, rnet::kController, 0x00);
    TEST_ASSERT_TRUE(b.put(body, sizeof(body)).finish(out));
    FrameParser p;
    rnet::ZoneParamValue v{};
    TEST_ASSERT_EQUAL(ReplyResult::kOk, rnet::parse_zone_param(parse(p, out.bytes, out.len), v));
    TEST_ASSERT_EQUAL_UINT8(5, v.zone);
    TEST_ASSERT_EQUAL(c.param, v.param);
    TEST_ASSERT_EQUAL_INT8(c.expected, v.value);
  }
}

void test_parse_handshake() {
  FrameParser p;
  uint8_t type = 0;
  TEST_ASSERT_EQUAL(ReplyResult::kOk,
                    rnet::parse_handshake(parse(p, kEventHandshake, sizeof(kEventHandshake)), type));
  TEST_ASSERT_EQUAL_HEX8(0x06, type);
  FrameParser p2;
  TEST_ASSERT_EQUAL(ReplyResult::kNotThisMessage,
                    rnet::parse_handshake(parse(p2, kZone1On, sizeof(kZone1On)), type));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_vector_zone1_volume_up);
  RUN_TEST(test_vector_zone1_volume_down);
  RUN_TEST(test_vector_zone_power);
  RUN_TEST(test_vector_all_zones);
  RUN_TEST(test_vector_select_source);
  RUN_TEST(test_vector_zone1_menu);
  RUN_TEST(test_vector_event_handshake);
  RUN_TEST(test_set_volume_template);
  RUN_TEST(test_set_zone_param_template);
  RUN_TEST(test_set_zone_param_encodings);
  RUN_TEST(test_get_zone_param_template);
  RUN_TEST(test_get_all_zone_info_template);
  RUN_TEST(test_session_start_device_restart);
  RUN_TEST(test_data_handshake_hypothesis);
  RUN_TEST(test_builders_reject_invalid_arguments);
  RUN_TEST(test_parse_all_zone_info_zone3);
  RUN_TEST(test_parse_all_zone_info_zone6_extremes);
  RUN_TEST(test_parse_all_zone_info_uses_path_not_length);
  RUN_TEST(test_parse_all_zone_info_rejects_bad_data);
  RUN_TEST(test_parse_all_zone_info_ignores_other_messages);
  RUN_TEST(test_parse_turn_on_volume_reply);
  RUN_TEST(test_parse_zone_param_signed_values);
  RUN_TEST(test_parse_handshake);
  return UNITY_END();
}
