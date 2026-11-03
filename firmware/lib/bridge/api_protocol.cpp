#include "api_protocol.h"

#include <ArduinoJson.h>
#include <cstring>

#include "rnet_messages.h"
#include "rnet_text.h"

namespace bridge {

namespace {

size_t finish(const JsonDocument& doc, char* out, size_t cap) {
  if (cap == 0 || measureJson(doc) + 1 > cap || doc.overflowed()) {
    if (cap > 0) {
      out[0] = '\0';
    }
    return 0;
  }
  return serializeJson(doc, out, cap);
}

const char* error_name(rnet::CommandError e) {
  switch (e) {
    case rnet::CommandError::kNone:
      return nullptr;
    case rnet::CommandError::kBusy:
      return "busy";
    case rnet::CommandError::kInvalid:
      return "invalid";
    case rnet::CommandError::kLinkDown:
      return "link_down";
  }
  return "invalid";
}

void fill_zone(JsonObject o, uint8_t zone, const rnet::ZoneState& z) {
  o["zone"] = zone;
  o["valid"] = z.valid;
  o["power"] = z.power;
  o["source"] = z.source;
  o["volume"] = z.volume;
  o["muted"] = z.muted;
  o["bass"] = z.bass;
  o["treble"] = z.treble;
  o["balance"] = z.balance;
  o["loudness"] = z.loudness;
  if (z.turn_on_volume_valid) {
    o["turn_on_volume"] = z.turn_on_volume;
  } else {
    o["turn_on_volume"] = nullptr;
  }
  o["shared_source"] = z.shared_source;
  o["system_on"] = z.system_on;
}

void fill_link(JsonObject o, const rnet::Engine& engine) {
  const rnet::EngineStats& s = engine.stats();
  o["up"] = engine.link_up();
  JsonObject st = o["stats"].to<JsonObject>();
  st["tx"] = s.tx;
  st["rx"] = s.rx;
  st["checksum_errors"] = s.checksum_errors;
  st["dropped"] = s.dropped;
  st["timeouts"] = s.timeouts;
  st["retries"] = s.retries;
  st["duplicate_replies"] = s.duplicate_replies;
  st["handshakes_rx"] = s.handshakes_rx;
  st["queue_depth"] = engine.queue_depth();
}

const char* network_name(Iface i) {
  switch (i) {
    case Iface::kEthernet:
      return "ethernet";
    case Iface::kWifi:
      return "wifi";
    case Iface::kNone:
      break;
  }
  return nullptr;
}

// Reads an integer field in [lo, hi]. JSON booleans are not integers here. 64-bit, because `long`
// is 32-bit on the ESP32 and ids span the full uint32 range.
bool get_int(JsonVariantConst v, int64_t lo, int64_t hi, int64_t& out) {
  if (!v.is<int64_t>() || v.is<bool>()) {
    return false;
  }
  const int64_t x = v.as<int64_t>();
  if (x < lo || x > hi) {
    return false;
  }
  out = x;
  return true;
}

struct CmdSpec {
  const char* name;
  rnet::CommandType type;
  rnet::ZoneParam param;
  enum Value : uint8_t { kNoValue, kBool, kInt } value;
  bool zone_optional;
  bool no_zone;
};

const CmdSpec kCommands[] = {
    {"set_power", rnet::CommandType::kSetPower, rnet::ZoneParam::kBass, CmdSpec::kBool, false, false},
    {"set_source", rnet::CommandType::kSetSource, rnet::ZoneParam::kBass, CmdSpec::kInt, false, false},
    {"set_volume", rnet::CommandType::kSetVolume, rnet::ZoneParam::kBass, CmdSpec::kInt, false, false},
    {"volume_up", rnet::CommandType::kVolumeUp, rnet::ZoneParam::kBass, CmdSpec::kNoValue, false, false},
    {"volume_down", rnet::CommandType::kVolumeDown, rnet::ZoneParam::kBass, CmdSpec::kNoValue, false,
     false},
    {"set_mute", rnet::CommandType::kSetMute, rnet::ZoneParam::kBass, CmdSpec::kBool, false, false},
    {"set_bass", rnet::CommandType::kSetParam, rnet::ZoneParam::kBass, CmdSpec::kInt, false, false},
    {"set_treble", rnet::CommandType::kSetParam, rnet::ZoneParam::kTreble, CmdSpec::kInt, false, false},
    {"set_balance", rnet::CommandType::kSetParam, rnet::ZoneParam::kBalance, CmdSpec::kInt, false,
     false},
    {"set_loudness", rnet::CommandType::kSetParam, rnet::ZoneParam::kLoudness, CmdSpec::kBool, false,
     false},
    {"set_turn_on_volume", rnet::CommandType::kSetParam, rnet::ZoneParam::kTurnOnVolume,
     CmdSpec::kInt, false, false},
    {"all_on", rnet::CommandType::kAllOn, rnet::ZoneParam::kBass, CmdSpec::kNoValue, false, true},
    {"all_off", rnet::CommandType::kAllOff, rnet::ZoneParam::kBass, CmdSpec::kNoValue, false, true},
    {"refresh", rnet::CommandType::kRefresh, rnet::ZoneParam::kBass, CmdSpec::kNoValue, true, false},
};

// Fills `cmd` from a command message. Range checks beyond the JSON types are left to the engine's
// submit(), which rejects out-of-range values with `invalid` (docs/protocol.md).
bool parse_command(JsonObjectConst o, rnet::Command& cmd) {
  const char* name = o["cmd"] | static_cast<const char*>(nullptr);
  if (name == nullptr) {
    return false;
  }
  const CmdSpec* spec = nullptr;
  for (const CmdSpec& s : kCommands) {
    if (std::strcmp(name, s.name) == 0) {
      spec = &s;
      break;
    }
  }
  if (spec == nullptr) {
    return false;
  }
  cmd.type = spec->type;
  cmd.param = spec->param;
  cmd.zone = 0;
  cmd.value = 0;

  int64_t zone = 0;
  if (!spec->no_zone) {
    if (o["zone"].isNull()) {
      if (!spec->zone_optional) {
        return false;
      }
    } else if (!get_int(o["zone"], 1, rnet::kNumZones, zone)) {
      return false;
    }
  }
  cmd.zone = static_cast<uint8_t>(zone);

  JsonVariantConst v = o["value"];
  switch (spec->value) {
    case CmdSpec::kNoValue:
      break;
    case CmdSpec::kBool:
      if (!v.is<bool>()) {
        return false;
      }
      cmd.value = v.as<bool>() ? 1 : 0;
      break;
    case CmdSpec::kInt: {
      int64_t value = 0;
      if (!get_int(v, -1000, 1000, value)) {
        return false;
      }
      cmd.value = static_cast<int16_t>(value);
      break;
    }
  }
  return true;
}

bool parse_level(const char* s, LogLevel& out) {
  static const LogLevel kLevels[] = {LogLevel::kDebug, LogLevel::kInfo, LogLevel::kWarn,
                                     LogLevel::kError, LogLevel::kOff};
  if (s == nullptr) {
    return false;
  }
  for (LogLevel l : kLevels) {
    if (std::strcmp(s, log_level_name(l)) == 0) {
      out = l;
      return true;
    }
  }
  return false;
}

}  // namespace

const char* log_level_name(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug:
      return "debug";
    case LogLevel::kInfo:
      return "info";
    case LogLevel::kWarn:
      return "warn";
    case LogLevel::kError:
      return "error";
    case LogLevel::kOff:
      return "off";
  }
  return "off";
}

size_t encode_hello(const BridgeInfo& info, bool auth_required, char* out, size_t cap) {
  JsonDocument doc;
  doc["type"] = "hello";
  doc["proto"] = kProtoVersion;
  doc["id"] = info.id;
  doc["fw"] = info.fw;
  doc["zones"] = rnet::kNumZones;
  doc["sources"] = rnet::kNumSources;
  doc["auth_required"] = auth_required;
  return finish(doc, out, cap);
}

size_t encode_info(const BridgeInfo& info, char* out, size_t cap) {
  JsonDocument doc;
  doc["id"] = info.id;
  doc["name"] = info.name;
  doc["fw"] = info.fw;
  doc["proto"] = kProtoVersion;
  doc["model"] = "rnet";
  doc["zones"] = rnet::kNumZones;
  doc["sources"] = rnet::kNumSources;
  doc["uptime_s"] = info.uptime_s;
  const char* net = network_name(info.network);
  if (net != nullptr) {
    doc["network"] = net;
  } else {
    doc["network"] = nullptr;
  }
  if (info.network == Iface::kWifi) {
    doc["wifi_rssi"] = info.wifi_rssi;
  } else {
    doc["wifi_rssi"] = nullptr;
  }
  return finish(doc, out, cap);
}

size_t encode_zone_state(uint8_t zone, const rnet::ZoneState& z, char* out, size_t cap) {
  JsonDocument doc;
  doc["type"] = "zone_state";
  fill_zone(doc.as<JsonObject>(), zone, z);
  return finish(doc, out, cap);
}

size_t encode_link(const rnet::Engine& engine, char* out, size_t cap) {
  JsonDocument doc;
  doc["type"] = "link";
  fill_link(doc.as<JsonObject>(), engine);
  return finish(doc, out, cap);
}

size_t encode_snapshot(const rnet::Engine& engine, const rnet::ZoneModel& model, char* out,
                       size_t cap) {
  JsonDocument doc;
  doc["type"] = "snapshot";
  fill_link(doc["link"].to<JsonObject>(), engine);
  JsonArray zones = doc["zones"].to<JsonArray>();
  for (uint8_t z = 1; z <= rnet::kNumZones; ++z) {
    fill_zone(zones.add<JsonObject>(), z, model.zone(z));
  }
  return finish(doc, out, cap);
}

size_t encode_result(uint32_t id, rnet::CommandError error, char* out, size_t cap) {
  JsonDocument doc;
  doc["type"] = "result";
  doc["id"] = id;
  doc["ok"] = error == rnet::CommandError::kNone;
  if (error != rnet::CommandError::kNone) {
    doc["error"] = error_name(error);
  }
  return finish(doc, out, cap);
}

size_t encode_log(LogLevel level, const char* msg, char* out, size_t cap) {
  JsonDocument doc;
  doc["type"] = "log";
  doc["level"] = log_level_name(level);
  doc["msg"] = msg;
  return finish(doc, out, cap);
}

size_t encode_frame(bool tx, const uint8_t* wire, size_t len, char* out, size_t cap) {
  char hex[rnet::kMaxWireBytes * 3 + 4];
  rnet::format_hex(wire, len, hex, sizeof(hex));
  JsonDocument doc;
  doc["type"] = "frame";
  doc["dir"] = tx ? "tx" : "rx";
  doc["hex"] = hex;
  return finish(doc, out, cap);
}

size_t encode_error(const char* error, char* out, size_t cap) {
  JsonDocument doc;
  doc["type"] = "error";
  doc["error"] = error;
  return finish(doc, out, cap);
}

ClientMsg parse_client_message(const char* json, size_t len) {
  ClientMsg m{};
  m.type = ClientMsgType::kBad;
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok || !doc.is<JsonObjectConst>()) {
    return m;
  }
  JsonObjectConst o = doc.as<JsonObjectConst>();
  const char* type = o["type"] | static_cast<const char*>(nullptr);
  if (type == nullptr) {
    return m;
  }

  int64_t id = 0;
  if (get_int(o["id"], 0, 0xFFFFFFFFLL, id)) {
    m.has_id = true;
    m.id = static_cast<uint32_t>(id);
  }

  if (std::strcmp(type, "auth") == 0) {
    const char* token = o["token"] | "";
    const size_t n = std::strlen(token);
    m.type = ClientMsgType::kAuth;
    m.valid = n <= kMaxTokenLen;
    if (m.valid) {
      std::memcpy(m.token, token, n + 1);
    }
  } else if (std::strcmp(type, "command") == 0) {
    m.type = ClientMsgType::kCommand;
    m.valid = m.has_id && parse_command(o, m.command);
    m.command.id = m.id;
  } else if (std::strcmp(type, "subscribe_logs") == 0) {
    m.type = ClientMsgType::kSubscribeLogs;
    m.valid = parse_level(o["level"] | static_cast<const char*>(nullptr), m.log_level);
  } else if (std::strcmp(type, "trace_frames") == 0) {
    m.type = ClientMsgType::kTraceFrames;
    m.valid = o["enabled"].is<bool>();
    m.trace_enabled = o["enabled"] | false;
  } else if (std::strcmp(type, "raw_send") == 0) {
    m.type = ClientMsgType::kRawSend;
    const char* hex = o["hex"] | static_cast<const char*>(nullptr);
    const bool fix = o["fix_checksum"] | false;
    size_t n = 0;
    m.valid = m.has_id && hex != nullptr &&
              rnet::parse_hex(hex, m.raw, sizeof(m.raw) - (fix ? 2 : 0), n) && n > 0;
    if (m.valid && fix) {
      m.raw[n] = rnet::checksum(m.raw, n);
      m.raw[n + 1] = rnet::kEndOfMessage;
      n += 2;
    }
    m.raw_len = m.valid ? n : 0;
  }
  return m;
}

}  // namespace bridge
