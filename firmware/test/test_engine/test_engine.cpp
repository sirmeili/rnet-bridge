// Engine tests against a scripted fake controller (SPEC §8): polling, replies, timeouts and link
// state, command handling and priority polls, handshakes, keypad traffic and duplicates.
#include <cstring>
#include <unity.h>

#include "../vectors.h"
#include "rnet_codec.h"
#include "rnet_engine.h"
#include "rnet_messages.h"
#include "zone_model.h"

using rnet::Command;
using rnet::CommandError;
using rnet::CommandResult;
using rnet::CommandType;
using rnet::Engine;
using rnet::EngineConfig;
using rnet::Frame;
using rnet::FrameBuilder;
using rnet::WireFrame;
using rnet::ZoneModel;
using rnet::ZoneParam;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint8_t kSetData = static_cast<uint8_t>(rnet::MsgType::kSetData);
constexpr uint8_t kRequest = static_cast<uint8_t>(rnet::MsgType::kRequestData);
constexpr uint8_t kHandshake = static_cast<uint8_t>(rnet::MsgType::kHandshake);
constexpr uint8_t kEvent = static_cast<uint8_t>(rnet::MsgType::kEvent);

struct SentFrame {
  WireFrame wire;
  Frame frame;
  uint32_t at_ms;
};

// Reply as the CAA6.6 does (docs/rnet-notes.md M2.2): F0 00 zz 70 00 00 7F 00 00 04 02 00 zz 07
// 00 00 01 00 0C 00 <11 fields> 00 cs F7.
WireFrame zone_info_reply(uint8_t zone, bool power, uint8_t source, uint8_t volume) {
  const uint8_t zz = static_cast<uint8_t>(zone - 1);
  FrameBuilder b({0x00, zz, 0x70}, rnet::kController, kSetData);
  const uint8_t body[] = {0x00, 0x04, 0x02, 0x00, zz,   0x07, 0x00, 0x00, 0x01, 0x00, 0x0C,
                          0x00, power ? uint8_t{1} : uint8_t{0},
                          static_cast<uint8_t>(source - 1), volume, 0x0A, 0x0A, 0x00, 0x0A, 0x01,
                          0x00, 0x00, 0x00, 0x00};
  b.put(body, sizeof(body));
  WireFrame w{};
  TEST_ASSERT_TRUE(b.finish(w));
  return w;
}

// F0 00 zz 70 00 00 7F 00 00 05 02 00 zz 00 04 00 00 01 00 01 00 vv cs F7 (M2.6).
WireFrame turn_on_volume_reply(uint8_t zone, uint8_t value) {
  const uint8_t zz = static_cast<uint8_t>(zone - 1);
  FrameBuilder b({0x00, zz, 0x70}, rnet::kController, kSetData);
  const uint8_t body[] = {0x00, 0x05, 0x02, 0x00, zz,   0x00, 0x04, 0x00,
                          0x00, 0x01, 0x00, 0x01, 0x00, value};
  b.put(body, sizeof(body));
  WireFrame w{};
  TEST_ASSERT_TRUE(b.finish(w));
  return w;
}

// The controller ACKing our Set Data: F0 00 zz 70 00 00 7F 02 02 cs F7 (M2.7).
WireFrame handshake_to_us(uint8_t zone) {
  WireFrame w{};
  TEST_ASSERT_TRUE(rnet::build_handshake({0x00, static_cast<uint8_t>(zone - 1), 0x70},
                                         rnet::kController, 0x02, w));
  return w;
}

// A scripted controller. It answers zone-info and turn-on-volume requests with its stored state
// (when `responsive`), ACKs Set Data, and records every frame the engine sends.
class FakeController : public rnet::ByteIo {
 public:
  static constexpr size_t kMaxSent = 256;
  static constexpr size_t kRxMax = 4096;

  bool responsive = true;
  uint8_t volume[rnet::kNumZones] = {30, 23, 24, 36, 33, 15};
  uint8_t turn_on_volume[rnet::kNumZones] = {35, 20, 20, 20, 20, 20};
  bool power[rnet::kNumZones] = {true, true, true, true, true, true};
  uint32_t now_ms = 0;

  int read() override {
    if (rx_pos_ == rx_len_) {
      rx_pos_ = rx_len_ = 0;  // everything consumed: reuse the buffer
      return -1;
    }
    return rx_[rx_pos_++];
  }

  void write(const uint8_t* bytes, size_t len) override {
    rnet::FrameParser p;
    rnet::ParseResult r = rnet::ParseResult::kNone;
    for (size_t i = 0; i < len; ++i) {
      r = p.feed(bytes[i]);
    }
    TEST_ASSERT_EQUAL_MESSAGE(rnet::ParseResult::kFrame, r, "engine sent an invalid frame");
    TEST_ASSERT_TRUE(count_ < kMaxSent);
    SentFrame& s = sent_[count_++];
    std::memcpy(s.wire.bytes, bytes, len);
    s.wire.len = len;
    s.frame = p.frame();
    s.at_ms = now_ms;
    if (responsive) {
      respond(s.frame);
    }
  }

  void inject(const WireFrame& w) { inject(w.bytes, w.len); }
  void inject(const uint8_t* bytes, size_t len) {
    TEST_ASSERT_TRUE(rx_len_ + len <= kRxMax);
    std::memcpy(rx_ + rx_len_, bytes, len);
    rx_len_ += len;
  }

  size_t count() const { return count_; }
  const SentFrame& sent(size_t i) const { return sent_[i]; }
  const SentFrame& last() const { return sent_[count_ - 1]; }

  size_t count_type(uint8_t type) const {
    size_t n = 0;
    for (size_t i = 0; i < count_; ++i) {
      if (sent_[i].frame.type == type) {
        ++n;
      }
    }
    return n;
  }

 private:
  void respond(const Frame& f) {
    const uint8_t zz = f.source.zone;
    if (f.type == kRequest && f.body_len >= 5 && f.body[0] == 0x04 && f.body[4] == 0x07) {
      inject(zone_info_reply(static_cast<uint8_t>(zz + 1), power[zz], 2, volume[zz]));
    } else if (f.type == kRequest && f.body_len >= 6 && f.body[0] == 0x05 && f.body[5] == 0x04) {
      inject(turn_on_volume_reply(static_cast<uint8_t>(zz + 1), turn_on_volume[zz]));
    } else if (f.type == kSetData) {
      inject(handshake_to_us(static_cast<uint8_t>(zz + 1)));
    } else if (f.type == kEvent && f.body_len >= 10 && f.body[4] == 0xDE) {
      // Set Volume event (SPEC §3.3): path 02 02 00, no source path, id DE 00, then the volume in
      // the timestamp field and the zone in the data field.
      volume[f.body[8]] = f.body[6];
    }
  }

  uint8_t rx_[kRxMax];
  size_t rx_len_ = 0;
  size_t rx_pos_ = 0;
  SentFrame sent_[kMaxSent];
  size_t count_ = 0;
};

struct Rig {
  FakeController io;
  ZoneModel model;
  Engine engine;
  uint32_t now = 0;

  explicit Rig(const EngineConfig& cfg = EngineConfig{}) : engine(io, model, cfg) {}

  // Ticks every millisecond up to and including `until`.
  void run_until(uint32_t until) {
    while (now <= until) {
      io.now_ms = now;
      engine.tick(now);
      ++now;
    }
  }
  void run_for(uint32_t ms) { run_until(now + ms); }

  // Runs until every zone has been polled once and the link is up.
  void bring_up() {
    run_for(3500);
    TEST_ASSERT_TRUE(engine.link_up());
    for (uint8_t z = 1; z <= rnet::kNumZones; ++z) {
      TEST_ASSERT_TRUE(model.zone(z).valid);
    }
    engine.take_link_changed();
    model.take_changed();
  }

  CommandError submit(CommandType type, uint8_t zone, int16_t value = 0,
                      ZoneParam param = ZoneParam::kBass, uint32_t id = 1) {
    return engine.submit(Command{id, type, zone, value, param});
  }

  CommandResult result() {
    CommandResult r{};
    TEST_ASSERT_TRUE_MESSAGE(engine.next_result(r), "no command result");
    return r;
  }
};

bool is_zone_info_request(const SentFrame& s, uint8_t zone) {
  return s.frame.type == kRequest && s.frame.body_len >= 5 && s.frame.body[0] == 0x04 &&
         s.frame.body[3] == zone - 1 && s.frame.body[4] == 0x07;
}

bool is_turn_on_volume_request(const SentFrame& s, uint8_t zone) {
  return s.frame.type == kRequest && s.frame.body_len >= 6 && s.frame.body[0] == 0x05 &&
         s.frame.body[3] == zone - 1 && s.frame.body[5] == 0x04;
}

bool same_wire(const SentFrame& s, const WireFrame& w) {
  return s.wire.len == w.len && std::memcmp(s.wire.bytes, w.bytes, w.len) == 0;
}

// --- Polling and replies ---

void test_round_robin_polls_every_zone_in_order() {
  Rig r;
  r.io.responsive = false;  // no replies: just look at what is polled first
  r.run_for(1);
  TEST_ASSERT_EQUAL_size_t(1, r.io.count());
  TEST_ASSERT_TRUE(is_zone_info_request(r.io.sent(0), 1));

  Rig q;
  q.run_for(3100);
  uint8_t expected = 1;
  uint32_t last_at = 0;
  size_t polls = 0;
  for (size_t i = 0; i < q.io.count(); ++i) {
    const SentFrame& s = q.io.sent(i);
    if (s.frame.type != kRequest || s.frame.body[0] != 0x04) {
      continue;  // turn-on volume polls after the link came up
    }
    TEST_ASSERT_TRUE(is_zone_info_request(s, expected));
    if (polls > 0) {
      TEST_ASSERT_UINT32_WITHIN(1, 500, s.at_ms - last_at);
    }
    last_at = s.at_ms;
    expected = static_cast<uint8_t>(expected % rnet::kNumZones + 1);
    ++polls;
  }
  TEST_ASSERT_EQUAL_size_t(7, polls);  // zones 1..6 then 1 again
}

void test_reply_updates_model_and_brings_link_up() {
  Rig r;
  TEST_ASSERT_FALSE(r.engine.link_up());
  r.run_for(10);
  TEST_ASSERT_TRUE(r.engine.link_up());
  TEST_ASSERT_TRUE(r.engine.take_link_changed());
  TEST_ASSERT_TRUE(r.model.zone(1).valid);
  TEST_ASSERT_EQUAL_UINT8(30, r.model.zone(1).volume);
  TEST_ASSERT_EQUAL_UINT8(2, r.model.zone(1).source);
}

void test_turn_on_volumes_polled_after_link_up() {
  Rig r;
  r.run_for(1000);
  for (uint8_t z = 1; z <= rnet::kNumZones; ++z) {
    TEST_ASSERT_TRUE(r.model.zone(z).turn_on_volume_valid);
  }
  TEST_ASSERT_EQUAL_UINT8(35, r.model.zone(1).turn_on_volume);
}

void test_turn_on_volume_repolled_every_60_s() {
  Rig r;
  r.run_for(1000);
  r.io.turn_on_volume[2] = 44;
  r.run_until(59000);
  TEST_ASSERT_EQUAL_UINT8(20, r.model.zone(3).turn_on_volume);
  r.run_until(62000);
  TEST_ASSERT_EQUAL_UINT8(44, r.model.zone(3).turn_on_volume);
}

void test_frames_are_at_least_the_minimum_gap_apart() {
  Rig r;
  r.bring_up();
  for (int i = 0; i < 5; ++i) {
    r.submit(CommandType::kSetSource, 1, 3, ZoneParam::kBass, static_cast<uint32_t>(i));
  }
  r.run_for(2000);
  for (size_t i = 1; i < r.io.count(); ++i) {
    TEST_ASSERT_TRUE(r.io.sent(i).at_ms - r.io.sent(i - 1).at_ms >= 50);
  }
}

// --- Handshakes (SPEC §3.5) ---

void test_no_data_ack_is_sent() {
  Rig r;
  r.bring_up();
  r.submit(CommandType::kSetParam, 1, 3, ZoneParam::kBass);
  r.run_for(1000);
  TEST_ASSERT_EQUAL_size_t(0, r.io.count_type(kHandshake));
  TEST_ASSERT_EQUAL_UINT32(0, r.engine.stats().acks_tx);
}

void test_data_ack_sent_when_enabled() {
  EngineConfig cfg;
  cfg.send_data_ack = true;
  Rig r(cfg);
  r.run_for(10);
  TEST_ASSERT_EQUAL_size_t(1, r.io.count_type(kHandshake));
  WireFrame expected{};
  TEST_ASSERT_TRUE(rnet::build_data_handshake(1, 0x02, expected));
  TEST_ASSERT_TRUE(same_wire(r.io.last(), expected));
}

void test_controller_handshake_counts_as_link_health() {
  Rig r;
  r.bring_up();
  r.submit(CommandType::kSetParam, 2, -1, ZoneParam::kTreble);
  r.run_for(100);
  TEST_ASSERT_EQUAL_UINT32(1, r.engine.stats().handshakes_rx);
}

// --- Timeouts and link state ---

void test_timeout_retries_then_gives_up() {
  Rig r;
  r.io.responsive = false;
  r.run_until(1499);
  // First attempt at 0, retries at 500 and 1000.
  TEST_ASSERT_EQUAL_size_t(3, r.io.count());
  for (size_t i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(is_zone_info_request(r.io.sent(i), 1));
  }
  TEST_ASSERT_EQUAL_UINT32(2, r.engine.stats().retries);
  r.run_until(1600);
  // Gave up on zone 1 at 1500, then moved on to zone 2.
  TEST_ASSERT_EQUAL_UINT32(3, r.engine.stats().timeouts);
  TEST_ASSERT_TRUE(is_zone_info_request(r.io.last(), 2));
}

void test_link_down_after_five_timeouts_and_recovers() {
  Rig r;
  r.bring_up();
  r.io.responsive = false;
  const uint32_t timeouts_before = r.engine.stats().timeouts;
  const uint32_t deadline = r.now + 5000;
  while (r.engine.link_up() && r.now < deadline) {
    r.run_for(0);  // one tick
  }
  TEST_ASSERT_FALSE(r.engine.link_up());
  // Down on exactly the 5th consecutive timeout, not before.
  TEST_ASSERT_EQUAL_UINT32(5, r.engine.stats().timeouts - timeouts_before);
  TEST_ASSERT_TRUE(r.engine.take_link_changed());

  r.io.responsive = true;
  r.run_for(2000);
  TEST_ASSERT_TRUE(r.engine.link_up());
  TEST_ASSERT_TRUE(r.engine.take_link_changed());
}

void test_commands_fail_with_link_down() {
  Rig r;
  r.io.responsive = false;
  TEST_ASSERT_EQUAL(CommandError::kNone, r.submit(CommandType::kSetPower, 1, 1));
  r.run_for(5);
  const CommandResult res = r.result();
  TEST_ASSERT_EQUAL_UINT32(1, res.id);
  TEST_ASSERT_EQUAL(CommandError::kLinkDown, res.error);
  TEST_ASSERT_EQUAL_size_t(0, r.io.count_type(kEvent));
}

void test_session_start_sent_on_link_up_when_configured() {
  EngineConfig cfg;
  cfg.session_start = rnet::SessionStartEvent::kDeviceRestart;
  Rig r(cfg);
  r.run_for(100);
  WireFrame restart{};
  TEST_ASSERT_TRUE(rnet::build_session_start(rnet::SessionStartEvent::kDeviceRestart, restart));
  TEST_ASSERT_TRUE(same_wire(r.io.sent(1), restart));  // right after the first reply
}

void test_no_session_start_by_default() {
  Rig r;
  r.run_for(3000);
  TEST_ASSERT_EQUAL_size_t(0, r.io.count_type(kEvent));
}

// --- Commands ---

void test_command_jumps_ahead_and_triggers_priority_poll() {
  Rig r;
  r.bring_up();
  const size_t before = r.io.count();
  TEST_ASSERT_EQUAL(CommandError::kNone, r.submit(CommandType::kSetPower, 4, 0));
  r.run_for(60);  // past the gap left by the last poll
  WireFrame expected{};
  TEST_ASSERT_TRUE(rnet::build_zone_power(4, false, expected));
  TEST_ASSERT_TRUE(same_wire(r.io.sent(before), expected));
  const uint32_t sent_at = r.io.sent(before).at_ms;
  TEST_ASSERT_EQUAL(CommandError::kNone, r.result().error);

  r.run_for(200);
  bool polled = false;
  for (size_t i = before + 1; i < r.io.count(); ++i) {
    if (is_zone_info_request(r.io.sent(i), 4)) {
      TEST_ASSERT_UINT32_WITHIN(5, 150, r.io.sent(i).at_ms - sent_at);
      polled = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(polled, "no priority poll of zone 4");
}

void test_command_frames_match_spec_vectors() {
  Rig r;
  r.bring_up();
  r.submit(CommandType::kAllOff, 0);
  r.submit(CommandType::kSetSource, 6, 6);
  r.submit(CommandType::kSetPower, 2, 1);
  r.run_for(500);
  size_t found = 0;
  for (size_t i = 0; i < r.io.count(); ++i) {
    const SentFrame& s = r.io.sent(i);
    const uint8_t* v[] = {kAllOff, kZone6Source6, kZone2On};
    const size_t n[] = {sizeof(kAllOff), sizeof(kZone6Source6), sizeof(kZone2On)};
    for (size_t k = 0; k < 3; ++k) {
      if (s.wire.len == n[k] && std::memcmp(s.wire.bytes, v[k], n[k]) == 0) {
        ++found;
      }
    }
  }
  TEST_ASSERT_EQUAL_size_t(3, found);
}

void test_all_off_polls_every_zone() {
  Rig r;
  r.bring_up();
  const size_t before = r.io.count();
  r.submit(CommandType::kAllOff, 0);
  r.run_for(700);
  bool polled[rnet::kNumZones] = {};
  for (size_t i = before; i < r.io.count(); ++i) {
    for (uint8_t z = 1; z <= rnet::kNumZones; ++z) {
      polled[z - 1] = polled[z - 1] || is_zone_info_request(r.io.sent(i), z);
    }
  }
  for (bool p : polled) {
    TEST_ASSERT_TRUE(p);
  }
}

void test_queue_full_is_busy() {
  Rig r;
  for (size_t i = 0; i < Engine::kQueueDepth; ++i) {
    TEST_ASSERT_EQUAL(CommandError::kNone, r.submit(CommandType::kSetPower, 1, 1));
  }
  TEST_ASSERT_EQUAL(CommandError::kBusy, r.submit(CommandType::kSetPower, 1, 1));
  TEST_ASSERT_EQUAL_size_t(Engine::kQueueDepth, r.engine.queue_depth());
}

void test_invalid_commands_are_rejected() {
  Rig r;
  TEST_ASSERT_EQUAL(CommandError::kInvalid, r.submit(CommandType::kSetPower, 0, 1));
  TEST_ASSERT_EQUAL(CommandError::kInvalid, r.submit(CommandType::kSetPower, 7, 1));
  TEST_ASSERT_EQUAL(CommandError::kInvalid, r.submit(CommandType::kSetPower, 1, 2));
  TEST_ASSERT_EQUAL(CommandError::kInvalid, r.submit(CommandType::kSetSource, 1, 0));
  TEST_ASSERT_EQUAL(CommandError::kInvalid, r.submit(CommandType::kSetSource, 1, 7));
  TEST_ASSERT_EQUAL(CommandError::kInvalid, r.submit(CommandType::kSetVolume, 1, 51));
  TEST_ASSERT_EQUAL(CommandError::kInvalid, r.submit(CommandType::kSetVolume, 1, -1));
  TEST_ASSERT_EQUAL(CommandError::kInvalid, r.submit(CommandType::kSetMute, 1, 2));
  TEST_ASSERT_EQUAL(CommandError::kInvalid,
                    r.submit(CommandType::kSetParam, 1, 11, ZoneParam::kBass));
  TEST_ASSERT_EQUAL(CommandError::kInvalid,
                    r.submit(CommandType::kSetParam, 1, 2, ZoneParam::kLoudness));
  TEST_ASSERT_EQUAL(CommandError::kInvalid, r.submit(CommandType::kRefresh, 7));
  TEST_ASSERT_EQUAL(CommandError::kNone, r.submit(CommandType::kRefresh, 0));
  TEST_ASSERT_EQUAL(CommandError::kNone, r.submit(CommandType::kAllOn, 0));
}

void test_volume_steps_use_the_cached_volume() {
  Rig r;
  r.bring_up();
  r.submit(CommandType::kVolumeUp, 1, 0, ZoneParam::kBass, 7);
  r.submit(CommandType::kVolumeUp, 1, 0, ZoneParam::kBass, 8);
  r.run_for(120);  // both sent before the priority poll confirms
  WireFrame v31{};
  WireFrame v32{};
  TEST_ASSERT_TRUE(rnet::build_set_volume(1, 31, v31));
  TEST_ASSERT_TRUE(rnet::build_set_volume(1, 32, v32));
  bool saw31 = false;
  bool saw32 = false;
  for (size_t i = 0; i < r.io.count(); ++i) {
    saw31 = saw31 || same_wire(r.io.sent(i), v31);
    saw32 = saw32 || same_wire(r.io.sent(i), v32);
  }
  TEST_ASSERT_TRUE(saw31);
  TEST_ASSERT_TRUE(saw32);
  r.run_for(300);
  TEST_ASSERT_EQUAL_UINT8(32, r.model.zone(1).volume);
}

void test_volume_step_before_first_poll_is_invalid() {
  Rig r;
  r.run_for(10);  // link up, only zone 1 polled so far
  r.submit(CommandType::kVolumeUp, 5, 0, ZoneParam::kBass, 9);
  r.run_for(60);
  const CommandResult res = r.result();
  TEST_ASSERT_EQUAL_UINT32(9, res.id);
  TEST_ASSERT_EQUAL(CommandError::kInvalid, res.error);
}

void test_mute_and_unmute_round_trip() {
  Rig r;
  r.bring_up();
  r.submit(CommandType::kSetMute, 1, 1);
  r.run_for(400);
  TEST_ASSERT_TRUE(r.model.zone(1).muted);
  TEST_ASSERT_EQUAL_UINT8(0, r.model.zone(1).volume);

  r.submit(CommandType::kSetMute, 1, 0);
  r.run_for(400);
  TEST_ASSERT_FALSE(r.model.zone(1).muted);
  TEST_ASSERT_EQUAL_UINT8(30, r.model.zone(1).volume);
}

void test_unmute_when_not_muted_sends_nothing() {
  Rig r;
  r.bring_up();
  const size_t events = r.io.count_type(kEvent);
  r.submit(CommandType::kSetMute, 1, 0, ZoneParam::kBass, 4);
  r.run_for(60);
  const CommandResult res = r.result();
  TEST_ASSERT_EQUAL_UINT32(4, res.id);
  TEST_ASSERT_EQUAL(CommandError::kNone, res.error);
  TEST_ASSERT_EQUAL_size_t(events, r.io.count_type(kEvent));
}

void test_set_turn_on_volume_is_repolled() {
  Rig r;
  r.bring_up();
  r.io.turn_on_volume[0] = 40;  // the fake doesn't apply Set Data; pretend the controller did
  const size_t before = r.io.count();
  r.submit(CommandType::kSetParam, 1, 40, ZoneParam::kTurnOnVolume);
  r.run_for(300);
  bool polled = false;
  for (size_t i = before; i < r.io.count(); ++i) {
    polled = polled || is_turn_on_volume_request(r.io.sent(i), 1);
  }
  TEST_ASSERT_TRUE(polled);
  TEST_ASSERT_EQUAL_UINT8(40, r.model.zone(1).turn_on_volume);
}

void test_refresh_polls_the_zone_now() {
  Rig r;
  r.bring_up();
  r.io.volume[3] = 12;
  const size_t before = r.io.count();
  r.submit(CommandType::kRefresh, 4);
  r.run_for(250);
  TEST_ASSERT_EQUAL(CommandError::kNone, r.result().error);
  TEST_ASSERT_EQUAL_UINT8(12, r.model.zone(4).volume);
  bool tov = false;
  for (size_t i = before; i < r.io.count(); ++i) {
    tov = tov || is_turn_on_volume_request(r.io.sent(i), 4);
  }
  TEST_ASSERT_TRUE(tov);
}

// --- Keypad traffic and duplicates ---

void test_keypad_traffic_is_ignored() {
  Rig r;
  r.bring_up();
  const auto rx_before = r.engine.stats().rx;
  // Keypad Vol Up event and the controller's loudness reply to that keypad (rnet-notes M2.1/M2.3).
  const uint8_t keypad_event[] = {0xF0, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x05, 0x03, 0x02,
                                  0x00, 0x00, 0x02, 0x04, 0x03, 0x7F, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x01, 0x18, 0xF7};
  const uint8_t reply_to_keypad[] = {0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x00, 0x03,
                                     0x04, 0x05, 0x00, 0x05, 0x02, 0x00, 0x00, 0x00, 0x02,
                                     0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x1F, 0xF7};
  for (int i = 0; i < 3; ++i) {
    r.io.inject(keypad_event, sizeof(keypad_event));
    r.io.inject(reply_to_keypad, sizeof(reply_to_keypad));
  }
  r.run_for(5);
  TEST_ASSERT_TRUE(r.engine.stats().rx >= rx_before + 6);  // a poll reply may also land here
  TEST_ASSERT_EQUAL_UINT32(0, r.engine.stats().checksum_errors);
  TEST_ASSERT_EQUAL_UINT32(0, r.engine.stats().duplicate_replies);
  TEST_ASSERT_EQUAL_HEX8(0, r.model.take_changed());
}

void test_retransmitted_reply_counts_as_duplicate() {
  Rig r;
  r.bring_up();
  // Repeat zone 2's last reply with nothing outstanding for zone 2, as a retransmission would.
  r.io.inject(zone_info_reply(2, true, 2, 23));
  r.run_for(5);
  TEST_ASSERT_EQUAL_UINT32(1, r.engine.stats().duplicate_replies);
}

void test_regular_polls_are_not_duplicates() {
  Rig r;
  r.run_for(10000);  // the same state polled over and over
  TEST_ASSERT_EQUAL_UINT32(0, r.engine.stats().duplicate_replies);
}

void test_partial_frame_dropped_after_inter_byte_timeout() {
  Rig r;
  r.bring_up();
  const uint8_t partial[] = {0xF0, 0x00, 0x00, 0x70, 0x00};
  r.io.inject(partial, sizeof(partial));
  r.run_for(100);
  TEST_ASSERT_EQUAL_UINT32(1, r.engine.stats().dropped);
  TEST_ASSERT_TRUE(r.engine.link_up());
}

// --- Raw frames and frame tracing ---

void test_raw_frame_is_sent_as_is() {
  Rig r;
  r.bring_up();
  TEST_ASSERT_EQUAL(CommandError::kNone, r.engine.submit_raw(42, kZone1Menu, sizeof(kZone1Menu)));
  r.run_for(60);
  const CommandResult res = r.result();
  TEST_ASSERT_EQUAL_UINT32(42, res.id);
  TEST_ASSERT_EQUAL(CommandError::kNone, res.error);
  bool sent = false;
  for (size_t i = 0; i < r.io.count(); ++i) {
    const SentFrame& s = r.io.sent(i);
    sent = sent || (s.wire.len == sizeof(kZone1Menu) &&
                    std::memcmp(s.wire.bytes, kZone1Menu, sizeof(kZone1Menu)) == 0);
  }
  TEST_ASSERT_TRUE(sent);
}

void test_raw_frame_limits() {
  Rig r;
  TEST_ASSERT_EQUAL(CommandError::kInvalid, r.engine.submit_raw(1, kZone1Menu, 0));
  uint8_t big[rnet::kMaxWireBytes + 1] = {};
  TEST_ASSERT_EQUAL(CommandError::kInvalid, r.engine.submit_raw(1, big, sizeof(big)));
  for (size_t i = 0; i < Engine::kRawSlots; ++i) {
    TEST_ASSERT_EQUAL(CommandError::kNone, r.engine.submit_raw(1, kZone1Menu, sizeof(kZone1Menu)));
  }
  TEST_ASSERT_EQUAL(CommandError::kBusy, r.engine.submit_raw(1, kZone1Menu, sizeof(kZone1Menu)));
  // Commands still fit: the raw slots are separate from the queue depth.
  TEST_ASSERT_EQUAL(CommandError::kNone, r.submit(CommandType::kSetPower, 1, 1));
}

void test_raw_frame_with_link_down_fails() {
  Rig r;
  r.io.responsive = false;
  r.engine.submit_raw(5, kZone1Menu, sizeof(kZone1Menu));
  r.run_for(5);
  TEST_ASSERT_EQUAL(CommandError::kLinkDown, r.result().error);
  // The slot is free again.
  for (size_t i = 0; i < Engine::kRawSlots; ++i) {
    TEST_ASSERT_EQUAL(CommandError::kNone, r.engine.submit_raw(1, kZone1Menu, sizeof(kZone1Menu)));
  }
}

void test_reply_to_a_raw_request_is_not_a_duplicate() {
  Rig r;
  r.bring_up();
  // A raw Get All Zone Info for zone 1, as `raw_send` would send it: the fake answers it.
  WireFrame req{};
  TEST_ASSERT_TRUE(rnet::build_get_all_zone_info(1, req));
  r.engine.submit_raw(1, req.bytes, req.len);
  r.run_for(100);
  TEST_ASSERT_EQUAL_UINT32(0, r.engine.stats().duplicate_replies);
  // A repeat long after the raw send still counts.
  r.run_for(1000);
  r.io.inject(zone_info_reply(2, true, 2, 23));
  r.run_for(5);
  TEST_ASSERT_EQUAL_UINT32(1, r.engine.stats().duplicate_replies);
}

struct TapLog {
  size_t tx = 0;
  size_t rx = 0;
  bool first_tx_is_poll = false;
};

void tap_cb(void* ctx, bool tx, const uint8_t* wire, size_t len) {
  auto* log = static_cast<TapLog*>(ctx);
  if (tx) {
    if (log->tx == 0) {
      WireFrame poll{};
      rnet::build_get_all_zone_info(1, poll);
      log->first_tx_is_poll = len == poll.len && std::memcmp(wire, poll.bytes, len) == 0;
    }
    ++log->tx;
  } else {
    ++log->rx;
  }
}

void test_frame_tap_sees_both_directions() {
  Rig r;
  TapLog log;
  r.engine.set_frame_tap(tap_cb, &log);
  r.run_for(1000);
  TEST_ASSERT_TRUE(log.first_tx_is_poll);
  TEST_ASSERT_EQUAL_size_t(r.engine.stats().tx, log.tx);
  TEST_ASSERT_EQUAL_size_t(r.engine.stats().rx, log.rx);
  TEST_ASSERT_TRUE(log.rx > 0);
}


// --- Turn-on volume keeps the live volume (CAA6.6, docs/rnet-notes.md) ---

size_t find_sent(const FakeController& io, const WireFrame& w, size_t from = 0) {
  for (size_t i = from; i < io.count(); ++i) {
    if (same_wire(io.sent(i), w)) {
      return i;
    }
  }
  return io.count();
}

void test_turn_on_volume_change_restores_volume_of_a_playing_zone() {
  Rig r;
  r.bring_up();
  const size_t before = r.io.count();
  r.submit(CommandType::kSetParam, 1, 40, ZoneParam::kTurnOnVolume);
  r.run_for(300);
  WireFrame tov{};
  WireFrame vol{};
  TEST_ASSERT_TRUE(rnet::build_set_zone_param(1, ZoneParam::kTurnOnVolume, 40, tov));
  TEST_ASSERT_TRUE(rnet::build_set_volume(1, 30, vol));  // zone 1's volume before
  const size_t i = find_sent(r.io, tov, before);
  TEST_ASSERT_TRUE_MESSAGE(i < r.io.count(), "turn-on volume not sent");
  TEST_ASSERT_TRUE_MESSAGE(i + 1 < r.io.count(), "nothing sent after it");
  TEST_ASSERT_TRUE_MESSAGE(same_wire(r.io.sent(i + 1), vol), "restore is not the next frame");
  const uint32_t gap = r.io.sent(i + 1).at_ms - r.io.sent(i).at_ms;
  TEST_ASSERT_TRUE(gap >= 50 && gap <= 60);
  TEST_ASSERT_EQUAL(CommandError::kNone, r.result().error);  // one result, for the command
  CommandResult extra{};
  TEST_ASSERT_FALSE(r.engine.next_result(extra));
}

void test_turn_on_volume_change_on_an_off_zone_sends_no_volume() {
  Rig r;
  r.io.power[1] = false;
  r.bring_up();
  const size_t before = r.io.count();
  r.submit(CommandType::kSetParam, 2, 40, ZoneParam::kTurnOnVolume);
  r.run_for(300);
  for (size_t i = before; i < r.io.count(); ++i) {
    const Frame& f = r.io.sent(i).frame;
    TEST_ASSERT_FALSE_MESSAGE(f.type == kEvent && f.body_len >= 6 && f.body[4] == 0xDE,
                              "Set Volume sent for an off zone");
  }
}

void test_turn_on_volume_restore_uses_the_commanded_volume() {
  Rig r;
  r.bring_up();
  r.submit(CommandType::kSetVolume, 1, 12, ZoneParam::kBass, 1);
  r.submit(CommandType::kSetParam, 1, 40, ZoneParam::kTurnOnVolume, 2);
  r.run_for(250);  // restores go ahead of polls, so this still uses the commanded 12
  WireFrame vol{};
  TEST_ASSERT_TRUE(rnet::build_set_volume(1, 12, vol));
  size_t count = 0;
  for (size_t i = 0; i < r.io.count(); ++i) {
    count += same_wire(r.io.sent(i), vol) ? 1 : 0;
  }
  TEST_ASSERT_EQUAL_size_t(2, count);  // the command, then the restore
}

void test_turn_on_volume_restore_can_be_turned_off() {
  EngineConfig cfg;
  cfg.restore_volume_after_turn_on_volume = false;
  Rig r(cfg);
  r.bring_up();
  const size_t before = r.io.count();
  r.submit(CommandType::kSetParam, 1, 40, ZoneParam::kTurnOnVolume);
  r.run_for(300);
  WireFrame vol{};
  TEST_ASSERT_TRUE(rnet::build_set_volume(1, 30, vol));
  TEST_ASSERT_EQUAL_size_t(r.io.count(), find_sent(r.io, vol, before));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_round_robin_polls_every_zone_in_order);
  RUN_TEST(test_reply_updates_model_and_brings_link_up);
  RUN_TEST(test_turn_on_volumes_polled_after_link_up);
  RUN_TEST(test_turn_on_volume_repolled_every_60_s);
  RUN_TEST(test_frames_are_at_least_the_minimum_gap_apart);
  RUN_TEST(test_no_data_ack_is_sent);
  RUN_TEST(test_data_ack_sent_when_enabled);
  RUN_TEST(test_controller_handshake_counts_as_link_health);
  RUN_TEST(test_timeout_retries_then_gives_up);
  RUN_TEST(test_link_down_after_five_timeouts_and_recovers);
  RUN_TEST(test_commands_fail_with_link_down);
  RUN_TEST(test_session_start_sent_on_link_up_when_configured);
  RUN_TEST(test_no_session_start_by_default);
  RUN_TEST(test_command_jumps_ahead_and_triggers_priority_poll);
  RUN_TEST(test_command_frames_match_spec_vectors);
  RUN_TEST(test_all_off_polls_every_zone);
  RUN_TEST(test_queue_full_is_busy);
  RUN_TEST(test_invalid_commands_are_rejected);
  RUN_TEST(test_volume_steps_use_the_cached_volume);
  RUN_TEST(test_volume_step_before_first_poll_is_invalid);
  RUN_TEST(test_mute_and_unmute_round_trip);
  RUN_TEST(test_unmute_when_not_muted_sends_nothing);
  RUN_TEST(test_set_turn_on_volume_is_repolled);
  RUN_TEST(test_refresh_polls_the_zone_now);
  RUN_TEST(test_keypad_traffic_is_ignored);
  RUN_TEST(test_retransmitted_reply_counts_as_duplicate);
  RUN_TEST(test_regular_polls_are_not_duplicates);
  RUN_TEST(test_partial_frame_dropped_after_inter_byte_timeout);
  RUN_TEST(test_raw_frame_is_sent_as_is);
  RUN_TEST(test_raw_frame_limits);
  RUN_TEST(test_raw_frame_with_link_down_fails);
  RUN_TEST(test_reply_to_a_raw_request_is_not_a_duplicate);
  RUN_TEST(test_frame_tap_sees_both_directions);
  RUN_TEST(test_turn_on_volume_change_restores_volume_of_a_playing_zone);
  RUN_TEST(test_turn_on_volume_change_on_an_off_zone_sends_no_volume);
  RUN_TEST(test_turn_on_volume_restore_uses_the_commanded_volume);
  RUN_TEST(test_turn_on_volume_restore_can_be_turned_off);
  return UNITY_END();
}
