// Tests for the proto 1 JSON messages (docs/protocol.md): encoders against expected JSON, and the
// client message parser, including every command name and the rejection cases.
#include <ArduinoJson.h>
#include <cstring>
#include <unity.h>

#include "../vectors.h"
#include "api_protocol.h"
#include "rnet_engine.h"
#include "zone_model.h"

using bridge::ClientMsg;
using bridge::ClientMsgType;
using bridge::Iface;
using rnet::CommandType;
using rnet::ZoneParam;

void setUp() {}
void tearDown() {}

namespace {

class NullIo : public rnet::ByteIo {
 public:
  int read() override { return -1; }
  void write(const uint8_t*, size_t) override {}
};

ClientMsg parse(const char* json) { return bridge::parse_client_message(json, std::strlen(json)); }

JsonDocument decode(const char* json) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json);
  TEST_ASSERT_TRUE_MESSAGE(err == DeserializationError::Ok, err.c_str());
  return doc;
}

const bridge::BridgeInfo kInfo{"a1b2c3d4e5f6", "rnet-bridge-e5f6", "0.0.3", 3600, Iface::kEthernet,
                               -60};

// --- Encoders ---

void test_hello() {
  char out[256];
  TEST_ASSERT_TRUE(bridge::encode_hello(kInfo, true, out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING(
      "{\"type\":\"hello\",\"proto\":1,\"id\":\"a1b2c3d4e5f6\",\"fw\":\"0.0.3\",\"zones\":6,"
      "\"sources\":6,\"auth_required\":true}",
      out);
}

void test_info_on_ethernet_has_null_rssi() {
  char out[256];
  TEST_ASSERT_TRUE(bridge::encode_info(kInfo, out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING(
      "{\"id\":\"a1b2c3d4e5f6\",\"name\":\"rnet-bridge-e5f6\",\"fw\":\"0.0.3\",\"proto\":1,"
      "\"model\":\"rnet\",\"zones\":6,\"sources\":6,\"uptime_s\":3600,\"network\":\"ethernet\","
      "\"wifi_rssi\":null}",
      out);
}

void test_info_on_wifi_has_rssi() {
  bridge::BridgeInfo info = kInfo;
  info.network = Iface::kWifi;
  char out[256];
  TEST_ASSERT_TRUE(bridge::encode_info(info, out, sizeof(out)) > 0);
  JsonDocument doc = decode(out);
  TEST_ASSERT_EQUAL_STRING("wifi", doc["network"]);
  TEST_ASSERT_EQUAL_INT(-60, doc["wifi_rssi"].as<int>());
}

void test_zone_state() {
  rnet::ZoneState z{};
  z.valid = true;
  z.power = true;
  z.source = 2;
  z.volume = 24;
  z.treble = -2;
  z.system_on = true;
  char out[512];
  TEST_ASSERT_TRUE(bridge::encode_zone_state(3, z, out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING(
      "{\"type\":\"zone_state\",\"zone\":3,\"valid\":true,\"power\":true,\"source\":2,\"volume\":24,"
      "\"muted\":false,\"bass\":0,\"treble\":-2,\"balance\":0,\"loudness\":false,"
      "\"turn_on_volume\":null,\"shared_source\":false,\"system_on\":true}",
      out);
  z.turn_on_volume_valid = true;
  z.turn_on_volume = 20;
  bridge::encode_zone_state(3, z, out, sizeof(out));
  TEST_ASSERT_EQUAL_INT(20, decode(out)["turn_on_volume"].as<int>());
}

void test_snapshot_and_link() {
  NullIo io;
  rnet::ZoneModel model;
  rnet::Engine engine(io, model);
  char out[4096];
  TEST_ASSERT_TRUE(bridge::encode_snapshot(engine, model, out, sizeof(out)) > 0);
  JsonDocument doc = decode(out);
  TEST_ASSERT_EQUAL_STRING("snapshot", doc["type"]);
  TEST_ASSERT_FALSE(doc["link"]["up"].as<bool>());
  TEST_ASSERT_EQUAL_size_t(6, doc["zones"].size());
  TEST_ASSERT_EQUAL_INT(6, doc["zones"][5]["zone"].as<int>());
  TEST_ASSERT_FALSE(doc["zones"][0]["valid"].as<bool>());
  TEST_ASSERT_TRUE(doc["zones"][0]["type"].isNull());

  TEST_ASSERT_TRUE(bridge::encode_link(engine, out, sizeof(out)) > 0);
  doc = decode(out);
  TEST_ASSERT_EQUAL_STRING("link", doc["type"]);
  for (const char* k : {"tx", "rx", "checksum_errors", "dropped", "timeouts", "retries",
                        "duplicate_replies", "handshakes_rx", "queue_depth"}) {
    TEST_ASSERT_TRUE_MESSAGE(doc["stats"][k].is<int>(), k);
  }
}

void test_results() {
  char out[128];
  bridge::encode_result(17, rnet::CommandError::kNone, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"result\",\"id\":17,\"ok\":true}", out);
  bridge::encode_result(4294967295u, rnet::CommandError::kBusy, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"result\",\"id\":4294967295,\"ok\":false,\"error\":\"busy\"}",
                           out);
  bridge::encode_result(1, rnet::CommandError::kLinkDown, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("link_down", decode(out)["error"]);
  bridge::encode_result(1, rnet::CommandError::kInvalid, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("invalid", decode(out)["error"]);
}

void test_log_frame_error() {
  char out[256];
  bridge::encode_log(bridge::LogLevel::kWarn, "link \"down\"", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"log\",\"level\":\"warn\",\"msg\":\"link \\\"down\\\"\"}", out);
  bridge::encode_frame(true, kZone1On, 7, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"frame\",\"dir\":\"tx\",\"hex\":\"F0 00 00 7F 00 00 70\"}", out);
  bridge::encode_error("unauthorized", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"error\",\"error\":\"unauthorized\"}", out);
}

void test_encoder_reports_too_small_buffer() {
  char out[16];
  TEST_ASSERT_EQUAL_size_t(0, bridge::encode_hello(kInfo, false, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
}

// --- Parser ---

void test_bad_messages() {
  TEST_ASSERT_EQUAL(ClientMsgType::kBad, parse("not json").type);
  TEST_ASSERT_EQUAL(ClientMsgType::kBad, parse("[1,2]").type);
  TEST_ASSERT_EQUAL(ClientMsgType::kBad, parse("{\"cmd\":\"set_power\"}").type);
  TEST_ASSERT_EQUAL(ClientMsgType::kBad, parse("{\"type\":\"teleport\"}").type);
}

void test_auth() {
  ClientMsg m = parse("{\"type\":\"auth\",\"token\":\"s3cret\"}");
  TEST_ASSERT_EQUAL(ClientMsgType::kAuth, m.type);
  TEST_ASSERT_TRUE(m.valid);
  TEST_ASSERT_EQUAL_STRING("s3cret", m.token);
  char json[200];
  std::snprintf(json, sizeof(json), "{\"type\":\"auth\",\"token\":\"%065d\"}", 0);  // 65 chars
  TEST_ASSERT_FALSE(parse(json).valid);
}

void test_every_command_name() {
  struct Case {
    const char* json;
    CommandType type;
    uint8_t zone;
    int16_t value;
    ZoneParam param;
  };
  const Case cases[] = {
      {"{\"cmd\":\"set_power\",\"zone\":3,\"value\":true}", CommandType::kSetPower, 3, 1, ZoneParam::kBass},
      {"{\"cmd\":\"set_power\",\"zone\":3,\"value\":false}", CommandType::kSetPower, 3, 0, ZoneParam::kBass},
      {"{\"cmd\":\"set_source\",\"zone\":1,\"value\":6}", CommandType::kSetSource, 1, 6, ZoneParam::kBass},
      {"{\"cmd\":\"set_volume\",\"zone\":2,\"value\":50}", CommandType::kSetVolume, 2, 50, ZoneParam::kBass},
      {"{\"cmd\":\"volume_up\",\"zone\":4}", CommandType::kVolumeUp, 4, 0, ZoneParam::kBass},
      {"{\"cmd\":\"volume_down\",\"zone\":4}", CommandType::kVolumeDown, 4, 0, ZoneParam::kBass},
      {"{\"cmd\":\"set_mute\",\"zone\":5,\"value\":true}", CommandType::kSetMute, 5, 1, ZoneParam::kBass},
      {"{\"cmd\":\"set_bass\",\"zone\":6,\"value\":-10}", CommandType::kSetParam, 6, -10, ZoneParam::kBass},
      {"{\"cmd\":\"set_treble\",\"zone\":6,\"value\":10}", CommandType::kSetParam, 6, 10,
       ZoneParam::kTreble},
      {"{\"cmd\":\"set_balance\",\"zone\":6,\"value\":-3}", CommandType::kSetParam, 6, -3,
       ZoneParam::kBalance},
      {"{\"cmd\":\"set_loudness\",\"zone\":6,\"value\":true}", CommandType::kSetParam, 6, 1,
       ZoneParam::kLoudness},
      {"{\"cmd\":\"set_turn_on_volume\",\"zone\":6,\"value\":20}", CommandType::kSetParam, 6, 20,
       ZoneParam::kTurnOnVolume},
      {"{\"cmd\":\"all_on\"}", CommandType::kAllOn, 0, 0, ZoneParam::kBass},
      {"{\"cmd\":\"all_off\"}", CommandType::kAllOff, 0, 0, ZoneParam::kBass},
      {"{\"cmd\":\"refresh\"}", CommandType::kRefresh, 0, 0, ZoneParam::kBass},
      {"{\"cmd\":\"refresh\",\"zone\":2}", CommandType::kRefresh, 2, 0, ZoneParam::kBass},
  };
  for (const Case& c : cases) {
    // Wrap the fields into a full command message with an id.
    char json[200];
    std::snprintf(json, sizeof(json), "{\"type\":\"command\",\"id\":9,%s", c.json + 1);
    const ClientMsg m = parse(json);
    TEST_ASSERT_EQUAL_MESSAGE(ClientMsgType::kCommand, m.type, json);
    TEST_ASSERT_TRUE_MESSAGE(m.valid, json);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(9, m.command.id, json);
    TEST_ASSERT_EQUAL_MESSAGE(c.type, m.command.type, json);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(c.zone, m.command.zone, json);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(c.value, m.command.value, json);
    if (c.type == CommandType::kSetParam) {
      TEST_ASSERT_EQUAL_MESSAGE(c.param, m.command.param, json);
    }
  }
}

void test_invalid_commands_keep_their_id() {
  const char* bad[] = {
      "{\"type\":\"command\",\"id\":5,\"cmd\":\"set_power\",\"zone\":3,\"value\":1}",   // not a bool
      "{\"type\":\"command\",\"id\":5,\"cmd\":\"set_volume\",\"zone\":3,\"value\":true}",
      "{\"type\":\"command\",\"id\":5,\"cmd\":\"set_volume\",\"zone\":3}",             // no value
      "{\"type\":\"command\",\"id\":5,\"cmd\":\"set_power\",\"value\":true}",          // no zone
      "{\"type\":\"command\",\"id\":5,\"cmd\":\"set_power\",\"zone\":7,\"value\":true}",
      "{\"type\":\"command\",\"id\":5,\"cmd\":\"set_power\",\"zone\":\"1\",\"value\":true}",
      "{\"type\":\"command\",\"id\":5,\"cmd\":\"warp\",\"zone\":1}",
  };
  for (const char* json : bad) {
    const ClientMsg m = parse(json);
    TEST_ASSERT_EQUAL_MESSAGE(ClientMsgType::kCommand, m.type, json);
    TEST_ASSERT_FALSE_MESSAGE(m.valid, json);
    TEST_ASSERT_TRUE_MESSAGE(m.has_id, json);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, m.id, json);
  }
  // Without an id there is nothing to answer: still invalid.
  const ClientMsg m = parse("{\"type\":\"command\",\"cmd\":\"all_off\"}");
  TEST_ASSERT_FALSE(m.valid);
  TEST_ASSERT_FALSE(m.has_id);
}

void test_full_uint32_id_range() {
  ClientMsg m = parse("{\"type\":\"command\",\"id\":4294967295,\"cmd\":\"all_off\"}");
  TEST_ASSERT_TRUE(m.valid);
  TEST_ASSERT_EQUAL_UINT32(4294967295u, m.command.id);
  m = parse("{\"type\":\"command\",\"id\":4294967296,\"cmd\":\"all_off\"}");
  TEST_ASSERT_FALSE(m.has_id);
  m = parse("{\"type\":\"command\",\"id\":-1,\"cmd\":\"all_off\"}");
  TEST_ASSERT_FALSE(m.has_id);
}

void test_out_of_range_values_reach_the_engine_as_invalid() {
  // The parser only checks JSON types; ranges are the engine's (one place, docs/protocol.md).
  ClientMsg m = parse("{\"type\":\"command\",\"id\":1,\"cmd\":\"set_volume\",\"zone\":1,\"value\":51}");
  TEST_ASSERT_TRUE(m.valid);
  NullIo io;
  rnet::ZoneModel model;
  rnet::Engine engine(io, model);
  TEST_ASSERT_EQUAL(rnet::CommandError::kInvalid, engine.submit(m.command));
}

void test_subscribe_and_trace() {
  ClientMsg m = parse("{\"type\":\"subscribe_logs\",\"level\":\"debug\"}");
  TEST_ASSERT_EQUAL(ClientMsgType::kSubscribeLogs, m.type);
  TEST_ASSERT_TRUE(m.valid);
  TEST_ASSERT_EQUAL(bridge::LogLevel::kDebug, m.log_level);
  TEST_ASSERT_EQUAL(bridge::LogLevel::kOff, parse("{\"type\":\"subscribe_logs\",\"level\":\"off\"}").log_level);
  TEST_ASSERT_FALSE(parse("{\"type\":\"subscribe_logs\",\"level\":\"loud\"}").valid);

  m = parse("{\"type\":\"trace_frames\",\"enabled\":true}");
  TEST_ASSERT_EQUAL(ClientMsgType::kTraceFrames, m.type);
  TEST_ASSERT_TRUE(m.valid);
  TEST_ASSERT_TRUE(m.trace_enabled);
  TEST_ASSERT_FALSE(parse("{\"type\":\"trace_frames\"}").valid);
}

void test_raw_send() {
  ClientMsg m = parse(
      "{\"type\":\"raw_send\",\"id\":18,\"hex\":\"F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 01 00 "
      "00 00 01\",\"fix_checksum\":true}");
  TEST_ASSERT_EQUAL(ClientMsgType::kRawSend, m.type);
  TEST_ASSERT_TRUE(m.valid);
  TEST_ASSERT_EQUAL_size_t(sizeof(kZone1On), m.raw_len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(kZone1On, m.raw, sizeof(kZone1On));  // checksum 12 + F7 appended

  m = parse("{\"type\":\"raw_send\",\"id\":18,\"hex\":\"F0 00 7F\"}");
  TEST_ASSERT_TRUE(m.valid);
  TEST_ASSERT_EQUAL_size_t(3, m.raw_len);

  TEST_ASSERT_FALSE(parse("{\"type\":\"raw_send\",\"id\":18,\"hex\":\"ZZ\"}").valid);
  TEST_ASSERT_FALSE(parse("{\"type\":\"raw_send\",\"id\":18,\"hex\":\"\"}").valid);
  TEST_ASSERT_FALSE(parse("{\"type\":\"raw_send\",\"hex\":\"F0\"}").valid);  // no id
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_hello);
  RUN_TEST(test_info_on_ethernet_has_null_rssi);
  RUN_TEST(test_info_on_wifi_has_rssi);
  RUN_TEST(test_zone_state);
  RUN_TEST(test_snapshot_and_link);
  RUN_TEST(test_results);
  RUN_TEST(test_log_frame_error);
  RUN_TEST(test_encoder_reports_too_small_buffer);
  RUN_TEST(test_bad_messages);
  RUN_TEST(test_auth);
  RUN_TEST(test_every_command_name);
  RUN_TEST(test_invalid_commands_keep_their_id);
  RUN_TEST(test_full_uint32_id_range);
  RUN_TEST(test_out_of_range_values_reach_the_engine_as_invalid);
  RUN_TEST(test_subscribe_and_trace);
  RUN_TEST(test_raw_send);
  return UNITY_END();
}
