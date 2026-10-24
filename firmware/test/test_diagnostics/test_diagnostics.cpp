// Tests for the M2 diagnostic helpers: hex text, frame descriptions, duplicate detection, the
// parser's raw-wire access, and the Zone On/Off source-zone option.
#include <cstring>
#include <unity.h>

#include "../vectors.h"
#include "rnet_codec.h"
#include "rnet_duplicate.h"
#include "rnet_messages.h"
#include "rnet_text.h"

using rnet::FrameParser;
using rnet::ParseResult;
using rnet::WireFrame;

void setUp() {}
void tearDown() {}

namespace {

void assert_contains(const char* haystack, const char* needle) {
  if (std::strstr(haystack, needle) == nullptr) {
    char msg[300];
    std::snprintf(msg, sizeof(msg), "'%s' not found in '%s'", needle, haystack);
    TEST_FAIL_MESSAGE(msg);
  }
}

const rnet::Frame& parse(FrameParser& p, const uint8_t* bytes, size_t len) {
  TEST_ASSERT_EQUAL(ParseResult::kFrame, feed_all(p, bytes, len));
  return p.frame();
}

// --- Hex text ---

void test_format_hex() {
  char out[64];
  TEST_ASSERT_EQUAL_size_t(20, rnet::format_hex(kZone1On, 7, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("F0 00 00 7F 00 00 70", out);
  rnet::format_hex(kZone1On, 0, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("", out);
}

void test_format_hex_truncates() {
  char out[12];  // room for "F0 00 00..." only
  rnet::format_hex(kZone1On, sizeof(kZone1On), out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("F0 00 00...", out);
}

void test_parse_hex_formats() {
  uint8_t buf[8];
  size_t len = 0;
  TEST_ASSERT_TRUE(rnet::parse_hex("F0 00 7f", buf, sizeof(buf), len));
  TEST_ASSERT_EQUAL_size_t(3, len);
  TEST_ASSERT_EQUAL_HEX8(0x7F, buf[2]);
  TEST_ASSERT_TRUE(rnet::parse_hex("f0007F", buf, sizeof(buf), len));
  TEST_ASSERT_EQUAL_size_t(3, len);
  TEST_ASSERT_TRUE(rnet::parse_hex("0xF0, 0x01", buf, sizeof(buf), len));
  TEST_ASSERT_EQUAL_size_t(2, len);
  TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]);
  TEST_ASSERT_TRUE(rnet::parse_hex("", buf, sizeof(buf), len));
  TEST_ASSERT_EQUAL_size_t(0, len);
}

void test_parse_hex_rejects_bad_input() {
  uint8_t buf[2];
  size_t len = 0;
  TEST_ASSERT_FALSE(rnet::parse_hex("F0 0", buf, sizeof(buf), len));   // odd digits
  TEST_ASSERT_FALSE(rnet::parse_hex("G0", buf, sizeof(buf), len));     // bad digit
  TEST_ASSERT_FALSE(rnet::parse_hex("01 02 03", buf, sizeof(buf), len));  // too many
}

// --- Descriptions ---

void test_describe_event() {
  FrameParser p;
  char out[160];
  rnet::describe(parse(p, kZone2On, sizeof(kZone2On)), out, sizeof(out));
  assert_contains(out, "Event 00.00.7F<-00.00.70");
  assert_contains(out, "id=0x00DC(ZoneOnOff)");
  assert_contains(out, "ts=0x0001 data=0x0001");
}

void test_describe_zone_info() {
  // All zone info, zone 3 (same frame as test_messages).
  const uint8_t reply[] = {0xF0, 0x00, 0x02, 0x70, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x04, 0x02, 0x00,
                           0x02, 0x07, 0x00, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x01, 0x01, 0x18, 0x0A,
                           0x08, 0x00, 0x0A, 0x01, 0x00, 0x00, 0x00, 0x00, 0x54, 0xF7};
  FrameParser p;
  char out[200];
  rnet::describe(parse(p, reply, sizeof(reply)), out, sizeof(out));
  assert_contains(out, "SetData 00.02.70<-00.00.7F ZoneInfo z3 on src=2 vol=24 bass=0 treble=-2");
}

void test_describe_handshake_and_unknown_set_data() {
  FrameParser p;
  char out[160];
  rnet::describe(parse(p, kEventHandshake, sizeof(kEventHandshake)), out, sizeof(out));
  assert_contains(out, "Handshake 00.00.60<-00.7D.00 type=0x06");

  // Set Data with an unrecognised path is shown structurally.
  const uint8_t body[] = {0x00, 0x03, 0x02, 0x00, 0x09, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x2A};
  WireFrame w{};
  TEST_ASSERT_TRUE(
      rnet::FrameBuilder({0x00, 0x00, 0x70}, rnet::kController, 0x00).put(body, sizeof(body)).finish(w));
  FrameParser p2;
  rnet::describe(parse(p2, w.bytes, w.len), out, sizeof(out));
  assert_contains(out, "tpath=- spath=02.00.09 data=[2A]");
}

void test_describe_truncates_safely() {
  FrameParser p;
  char out[8];
  rnet::describe(parse(p, kZone1On, sizeof(kZone1On)), out, sizeof(out));
  TEST_ASSERT_EQUAL_size_t(7, std::strlen(out));
}

// --- Duplicate detection ---

void test_duplicate_detected_without_transmit() {
  rnet::DuplicateDetector d;
  uint32_t age = 0;
  TEST_ASSERT_FALSE(d.on_receive(kZone1On, sizeof(kZone1On), 1000, age));
  TEST_ASSERT_FALSE(d.on_receive(kZone1Off, sizeof(kZone1Off), 1100, age));
  TEST_ASSERT_TRUE(d.on_receive(kZone1On, sizeof(kZone1On), 3500, age));
  TEST_ASSERT_EQUAL_UINT32(2500, age);
}

void test_transmit_clears_history() {
  rnet::DuplicateDetector d;
  uint32_t age = 0;
  TEST_ASSERT_FALSE(d.on_receive(kZone1On, sizeof(kZone1On), 1000, age));
  d.on_transmit();
  TEST_ASSERT_FALSE(d.on_receive(kZone1On, sizeof(kZone1On), 1500, age));
}

void test_history_evicts_oldest() {
  rnet::DuplicateDetector d;
  uint32_t age = 0;
  const Vector* v = kFrameVectors;
  for (size_t i = 0; i < rnet::DuplicateDetector::kHistory + 1; ++i) {
    TEST_ASSERT_FALSE(d.on_receive(v[i].bytes, v[i].len, static_cast<uint32_t>(i), age));
  }
  // v[0] was evicted; v[1] is still remembered.
  TEST_ASSERT_FALSE(d.on_receive(v[0].bytes, v[0].len, 100, age));
  TEST_ASSERT_TRUE(d.on_receive(v[2].bytes, v[2].len, 100, age));
}

// --- Parser raw-wire access ---

void test_parser_exposes_last_wire_bytes() {
  FrameParser p;
  TEST_ASSERT_EQUAL(ParseResult::kFrame, feed_all(p, kZone1Menu, sizeof(kZone1Menu)));
  TEST_ASSERT_EQUAL_size_t(sizeof(kZone1Menu), p.last_wire_len());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(kZone1Menu, p.last_wire(), sizeof(kZone1Menu));

  uint8_t bad[sizeof(kZone1On)];
  std::memcpy(bad, kZone1On, sizeof(bad));
  bad[sizeof(bad) - 2] ^= 0x01;
  TEST_ASSERT_EQUAL(ParseResult::kChecksumError, feed_all(p, bad, sizeof(bad)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(bad, p.last_wire(), sizeof(bad));

  p.feed(0x00);  // any further byte invalidates it
  TEST_ASSERT_EQUAL_size_t(0, p.last_wire_len());
}

// --- Zone On/Off source-zone option (SPEC §11 item 8) ---

void test_zone_power_source_zone_variant() {
  WireFrame out{};
  TEST_ASSERT_TRUE(rnet::build_zone_power(2, true, out, rnet::PowerSourceZone::kZone));
  // PDF §8.1.1 template form: byte 6 = 01, checksum 0x14.
  const uint8_t expected[] = {0xF0, 0x00, 0x00, 0x7F, 0x00, 0x01, 0x70, 0x05, 0x02, 0x02, 0x00, 0x00,
                              0xF1, 0x23, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x14, 0xF7};
  assert_wire_equals(expected, sizeof(expected), out, "Zone 2 On (zz form)");
  TEST_ASSERT_TRUE(rnet::build_zone_power(2, true, out, rnet::PowerSourceZone::kZero));
  assert_wire_equals(kZone2On, sizeof(kZone2On), out, "Zone 2 On (default)");
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_format_hex);
  RUN_TEST(test_format_hex_truncates);
  RUN_TEST(test_parse_hex_formats);
  RUN_TEST(test_parse_hex_rejects_bad_input);
  RUN_TEST(test_describe_event);
  RUN_TEST(test_describe_zone_info);
  RUN_TEST(test_describe_handshake_and_unknown_set_data);
  RUN_TEST(test_describe_truncates_safely);
  RUN_TEST(test_duplicate_detected_without_transmit);
  RUN_TEST(test_transmit_clears_history);
  RUN_TEST(test_history_evicts_oldest);
  RUN_TEST(test_parser_exposes_last_wire_bytes);
  RUN_TEST(test_zone_power_source_zone_variant);
  return UNITY_END();
}
