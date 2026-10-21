// Codec tests: checksum, escaping, frame building, streaming parser and resync (SPEC §3.1, §8).
#include <unity.h>

#include "../vectors.h"
#include "rnet_codec.h"

using rnet::FrameBuilder;
using rnet::FrameParser;
using rnet::ParseResult;
using rnet::WireFrame;

void setUp() {}
void tearDown() {}

namespace {

constexpr rnet::DeviceId kTarget{0x00, 0x00, 0x7F};
constexpr rnet::DeviceId kSource{0x00, 0x00, 0x70};

void test_checksum_doc_example() {
  // PDF §4.3.1: F0 00 67 7C F1 0F -> 0x59.
  TEST_ASSERT_EQUAL_HEX8(0x59, rnet::checksum(kChecksumExample, sizeof(kChecksumExample) - 2));
}

void test_checksum_invert_example() {
  // PDF §4.2.1: F0 00 67 7C F1 6A -> 0x34 (0x95 escaped as F1 6A).
  const uint8_t wire[] = {0xF0, 0x00, 0x67, 0x7C, 0xF1, 0x6A};
  TEST_ASSERT_EQUAL_HEX8(0x34, rnet::checksum(wire, sizeof(wire)));
}

void test_checksum_of_every_frame_vector() {
  for (const Vector& v : kFrameVectors) {
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(v.bytes[v.len - 2], rnet::checksum(v.bytes, v.len - 2), v.name);
  }
}

void test_every_frame_vector_decodes_and_reencodes() {
  for (const Vector& v : kFrameVectors) {
    FrameParser p;
    TEST_ASSERT_EQUAL_MESSAGE(ParseResult::kFrame, feed_all(p, v.bytes, v.len), v.name);
    WireFrame out{};
    TEST_ASSERT_TRUE_MESSAGE(rnet::encode(p.frame(), out), v.name);
    assert_wire_equals(v.bytes, v.len, out, v.name);
  }
}

void test_escape_round_trip_0x80_to_0xff() {
  for (unsigned b = 0x80; b <= 0xFF; ++b) {
    const auto value = static_cast<uint8_t>(b);
    WireFrame out{};
    TEST_ASSERT_TRUE(FrameBuilder(kTarget, kSource, 0x05).put(value).finish(out));
    // F0 + 7 header bytes, then F1 ~b, checksum, F7.
    TEST_ASSERT_EQUAL_size_t(12, out.len);
    TEST_ASSERT_EQUAL_HEX8(rnet::kInvert, out.bytes[8]);
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(~value), out.bytes[9]);
    TEST_ASSERT_TRUE(out.bytes[9] <= 0x7F);

    FrameParser p;
    TEST_ASSERT_EQUAL(ParseResult::kFrame, feed_all(p, out.bytes, out.len));
    TEST_ASSERT_EQUAL_size_t(1, p.frame().body_len);
    TEST_ASSERT_EQUAL_HEX8(value, p.frame().body[0]);
  }
}

void test_bytes_up_to_0x7f_are_not_escaped() {
  for (unsigned b = 0x00; b <= 0x7F; ++b) {
    WireFrame out{};
    TEST_ASSERT_TRUE(FrameBuilder(kTarget, kSource, 0x05).put(static_cast<uint8_t>(b)).finish(out));
    TEST_ASSERT_EQUAL_size_t(11, out.len);
    TEST_ASSERT_EQUAL_HEX8(b, out.bytes[8]);
  }
}

void test_builder_rejects_overflow() {
  FrameBuilder b(kTarget, kSource, 0x05);
  for (int i = 0; i < 40; ++i) {
    b.put(0xFF);  // 2 wire bytes each
  }
  WireFrame out{};
  TEST_ASSERT_FALSE(b.finish(out));
  TEST_ASSERT_EQUAL_size_t(0, out.len);
}

void test_parser_ignores_garbage_before_start() {
  FrameParser p;
  const uint8_t garbage[] = {0x00, 0x7F, 0xF7, 0x13, 0xF1, 0x55};
  TEST_ASSERT_EQUAL(ParseResult::kNone, feed_all(p, garbage, sizeof(garbage)));
  TEST_ASSERT_FALSE(p.in_frame());
  TEST_ASSERT_EQUAL(ParseResult::kFrame, feed_all(p, kZone1On, sizeof(kZone1On)));
  WireFrame out{};
  TEST_ASSERT_TRUE(rnet::encode(p.frame(), out));
  assert_wire_equals(kZone1On, sizeof(kZone1On), out, "after garbage");
}

void test_parser_resyncs_on_truncated_frame() {
  FrameParser p;
  // First 10 bytes of a frame, then a complete frame starting with F0.
  TEST_ASSERT_EQUAL(ParseResult::kNone, feed_all(p, kZone1Off, 10));
  TEST_ASSERT_TRUE(p.in_frame());
  TEST_ASSERT_EQUAL(ParseResult::kResync, p.feed(kZone1On[0]));
  TEST_ASSERT_EQUAL(ParseResult::kFrame, feed_all(p, kZone1On + 1, sizeof(kZone1On) - 1));
  // Decoded body: 02 02 00 | 00 | DC 00 | ts lo, ts hi | ... ; timestamp lo = 1 (on).
  TEST_ASSERT_EQUAL_HEX8(0x01, p.frame().body[6]);
}

void test_parser_reset_drops_partial_frame() {
  FrameParser p;
  feed_all(p, kZone1Off, 10);
  p.reset();
  TEST_ASSERT_FALSE(p.in_frame());
  // Remainder of the old frame is ignored (no F0), including its F7.
  TEST_ASSERT_EQUAL(ParseResult::kNone, feed_all(p, kZone1Off + 10, sizeof(kZone1Off) - 10));
}

void test_parser_rejects_bad_checksum_then_recovers() {
  uint8_t bad[sizeof(kZone1On)];
  for (size_t i = 0; i < sizeof(bad); ++i) {
    bad[i] = kZone1On[i];
  }
  bad[sizeof(bad) - 2] ^= 0x01;
  FrameParser p;
  TEST_ASSERT_EQUAL(ParseResult::kChecksumError, feed_all(p, bad, sizeof(bad)));
  TEST_ASSERT_FALSE(p.in_frame());
  TEST_ASSERT_EQUAL(ParseResult::kFrame, feed_all(p, kZone1On, sizeof(kZone1On)));
}

void test_parser_rejects_corrupted_body_byte() {
  uint8_t bad[sizeof(kZone1On)];
  for (size_t i = 0; i < sizeof(bad); ++i) {
    bad[i] = kZone1On[i];
  }
  bad[15] = 0x02;  // body changed, checksum not updated
  FrameParser p;
  TEST_ASSERT_EQUAL(ParseResult::kChecksumError, feed_all(p, bad, sizeof(bad)));
}

void test_parser_rejects_dangling_invert() {
  // Header, then F1 immediately before the checksum.
  uint8_t wire[] = {0xF0, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x70, 0x05, 0xF1, 0x00, 0xF7};
  wire[9] = rnet::checksum(wire, 9);
  FrameParser p;
  TEST_ASSERT_EQUAL(ParseResult::kMalformed, feed_all(p, wire, sizeof(wire)));
}

void test_parser_rejects_stray_high_byte() {
  uint8_t wire[] = {0xF0, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x70, 0x05, 0xF5, 0x00, 0xF7};
  wire[9] = rnet::checksum(wire, 9);
  FrameParser p;
  TEST_ASSERT_EQUAL(ParseResult::kMalformed, feed_all(p, wire, sizeof(wire)));
}

void test_parser_rejects_short_frame() {
  FrameParser p;
  // Valid checksum but only 4 content bytes: the doc's checksum example is not a full frame.
  TEST_ASSERT_EQUAL(ParseResult::kMalformed, feed_all(p, kChecksumExample, sizeof(kChecksumExample)));
  const uint8_t empty[] = {0xF0, 0xF7};
  TEST_ASSERT_EQUAL(ParseResult::kMalformed, feed_all(p, empty, sizeof(empty)));
}

void test_parser_overflow_discards_until_next_start() {
  FrameParser p;
  p.feed(0xF0);
  ParseResult r = ParseResult::kNone;
  int overflow_at = -1;
  for (int i = 0; i < 100 && overflow_at < 0; ++i) {
    r = p.feed(0x01);
    if (r == ParseResult::kOverflow) {
      overflow_at = i;
    }
  }
  TEST_ASSERT_TRUE(overflow_at > 0);
  TEST_ASSERT_FALSE(p.in_frame());
  // A trailing F7 from the oversized frame is ignored, then a good frame parses.
  TEST_ASSERT_EQUAL(ParseResult::kNone, p.feed(0xF7));
  TEST_ASSERT_EQUAL(ParseResult::kFrame, feed_all(p, kZone1On, sizeof(kZone1On)));
}

void test_parser_back_to_back_frames() {
  FrameParser p;
  int frames = 0;
  for (const Vector& v : kFrameVectors) {
    for (size_t i = 0; i < v.len; ++i) {
      if (p.feed(v.bytes[i]) == ParseResult::kFrame) {
        ++frames;
      }
    }
  }
  TEST_ASSERT_EQUAL(static_cast<int>(sizeof(kFrameVectors) / sizeof(kFrameVectors[0])), frames);
}

void test_parser_decodes_header_fields() {
  FrameParser p;
  TEST_ASSERT_EQUAL(ParseResult::kFrame, feed_all(p, kEventHandshake, sizeof(kEventHandshake)));
  const rnet::Frame& f = p.frame();
  TEST_ASSERT_EQUAL_HEX8(0x00, f.target.controller);
  TEST_ASSERT_EQUAL_HEX8(0x00, f.target.zone);
  TEST_ASSERT_EQUAL_HEX8(0x60, f.target.keypad);
  TEST_ASSERT_EQUAL_HEX8(0x00, f.source.controller);
  TEST_ASSERT_EQUAL_HEX8(0x7D, f.source.zone);
  TEST_ASSERT_EQUAL_HEX8(0x00, f.source.keypad);
  TEST_ASSERT_EQUAL_HEX8(0x02, f.type);
  TEST_ASSERT_EQUAL_size_t(1, f.body_len);
  TEST_ASSERT_EQUAL_HEX8(0x06, f.body[0]);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_checksum_doc_example);
  RUN_TEST(test_checksum_invert_example);
  RUN_TEST(test_checksum_of_every_frame_vector);
  RUN_TEST(test_every_frame_vector_decodes_and_reencodes);
  RUN_TEST(test_escape_round_trip_0x80_to_0xff);
  RUN_TEST(test_bytes_up_to_0x7f_are_not_escaped);
  RUN_TEST(test_builder_rejects_overflow);
  RUN_TEST(test_parser_ignores_garbage_before_start);
  RUN_TEST(test_parser_resyncs_on_truncated_frame);
  RUN_TEST(test_parser_reset_drops_partial_frame);
  RUN_TEST(test_parser_rejects_bad_checksum_then_recovers);
  RUN_TEST(test_parser_rejects_corrupted_body_byte);
  RUN_TEST(test_parser_rejects_dangling_invert);
  RUN_TEST(test_parser_rejects_stray_high_byte);
  RUN_TEST(test_parser_rejects_short_frame);
  RUN_TEST(test_parser_overflow_discards_until_next_start);
  RUN_TEST(test_parser_back_to_back_frames);
  RUN_TEST(test_parser_decodes_header_fields);
  return UNITY_END();
}
