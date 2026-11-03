// JSON messages of the network API, proto 1 (docs/protocol.md). Encoders write server messages
// from engine and model state; the parser turns client messages into engine commands. Pure C++17
// on ArduinoJson, so it is tested on the host. ArduinoJson 7 allocates its document on the heap;
// that is confined to this network layer and never touches the RNET RX/TX path.
#pragma once

#include <cstddef>
#include <cstdint>

#include "network_policy.h"
#include "rnet_codec.h"
#include "rnet_engine.h"
#include "zone_model.h"

namespace bridge {

constexpr int kProtoVersion = 1;
constexpr size_t kMaxTokenLen = 64;

struct BridgeInfo {
  const char* id;    // lowercase hex MAC
  const char* name;  // hostname
  const char* fw;
  uint32_t uptime_s;
  Iface network;
  int wifi_rssi;  // dBm; only sent when network == kWifi
};

enum class LogLevel : uint8_t { kDebug, kInfo, kWarn, kError, kOff };

// Encoders write a NUL-terminated JSON object into `out` and return its length, or 0 if it doesn't
// fit in `cap`.
size_t encode_hello(const BridgeInfo& info, bool auth_required, char* out, size_t cap);
size_t encode_info(const BridgeInfo& info, char* out, size_t cap);  // GET /api/info body
size_t encode_zone_state(uint8_t zone, const rnet::ZoneState& z, char* out, size_t cap);
size_t encode_link(const rnet::Engine& engine, char* out, size_t cap);
size_t encode_snapshot(const rnet::Engine& engine, const rnet::ZoneModel& model, char* out,
                       size_t cap);
size_t encode_result(uint32_t id, rnet::CommandError error, char* out, size_t cap);
size_t encode_log(LogLevel level, const char* msg, char* out, size_t cap);
size_t encode_frame(bool tx, const uint8_t* wire, size_t len, char* out, size_t cap);
size_t encode_error(const char* error, char* out, size_t cap);

const char* log_level_name(LogLevel level);

enum class ClientMsgType : uint8_t {
  kBad,            // not JSON, no/unknown type: reply {"type":"error","error":"bad_message"}
  kAuth,
  kCommand,        // command is valid, or has_id && !valid: reply result invalid
  kSubscribeLogs,
  kTraceFrames,
  kRawSend,        // raw/raw_len hold complete wire bytes, or !valid: reply result invalid
};

struct ClientMsg {
  ClientMsgType type;
  bool has_id;
  uint32_t id;
  bool valid;  // command / raw_send fields were complete and in range
  char token[kMaxTokenLen + 1];
  rnet::Command command;
  LogLevel log_level;
  bool trace_enabled;
  uint8_t raw[rnet::kMaxWireBytes];
  size_t raw_len;
};

ClientMsg parse_client_message(const char* json, size_t len);

}  // namespace bridge
