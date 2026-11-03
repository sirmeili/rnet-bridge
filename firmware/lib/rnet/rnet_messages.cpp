#include "rnet_messages.h"

namespace rnet {

namespace {

constexpr uint8_t kPriorityLow = 0x01;  // low priority events do not generate a handshake
constexpr uint8_t kPathRunMenu = 0x02;  // event target path 2.2.0 (Root Menu / Run Menu)
constexpr uint8_t kPathSource = 0x00;   // event target path 2.0.0, used by Select Source
constexpr uint8_t kZoneInfoBranch = 0x07;
constexpr size_t kZoneInfoFields = 11;  // PDF §8.12 bytes 21..31

bool valid_zone(uint8_t zone) { return zone >= 1 && zone <= kNumZones; }

uint8_t wire_zone(uint8_t zone) { return static_cast<uint8_t>(zone - 1); }

DeviceId bridge_id(uint8_t wire_zone_id) { return {0x00, wire_zone_id, kBridgeKeypadId}; }

bool fail(WireFrame& out) {
  out.len = 0;
  return false;
}

// Event body: target path 2.<level1>.0, no source path, id, timestamp, data, priority.
bool build_event(const DeviceId& target, const DeviceId& source, uint8_t level1, uint16_t id,
                 uint16_t timestamp, uint16_t data, WireFrame& out) {
  FrameBuilder b(target, source, static_cast<uint8_t>(MsgType::kEvent));
  b.put(0x02).put(level1).put(0x00);  // target path
  b.put(0x00);                        // source path: none
  b.put_u16(id).put_u16(timestamp).put_u16(data).put(kPriorityLow);
  return b.finish(out);
}

// Wire encoding for a zone parameter value; false if `value` is out of range for `param`.
bool param_to_wire(ZoneParam param, int8_t value, uint8_t& wire) {
  switch (param) {
    case ZoneParam::kBass:
    case ZoneParam::kTreble:
    case ZoneParam::kBalance:
      if (value < kMinTone || value > kMaxTone) {
        return false;
      }
      wire = static_cast<uint8_t>(value - kMinTone);
      return true;
    case ZoneParam::kLoudness:
      if (value != 0 && value != 1) {
        return false;
      }
      wire = static_cast<uint8_t>(value);
      return true;
    case ZoneParam::kTurnOnVolume:
      if (value < 0 || value > kMaxVolume) {
        return false;
      }
      wire = static_cast<uint8_t>(value);
      return true;
  }
  return false;
}

bool param_from_wire(ZoneParam param, uint8_t wire, int8_t& value) {
  switch (param) {
    case ZoneParam::kBass:
    case ZoneParam::kTreble:
    case ZoneParam::kBalance:
      if (wire > kMaxTone - kMinTone) {
        return false;
      }
      value = static_cast<int8_t>(wire + kMinTone);
      return true;
    case ZoneParam::kLoudness:
      if (wire > 1) {
        return false;
      }
      value = static_cast<int8_t>(wire);
      return true;
    case ZoneParam::kTurnOnVolume:
      if (wire > kMaxVolume) {
        return false;
      }
      value = static_cast<int8_t>(wire);
      return true;
  }
  return false;
}

bool valid_param(uint8_t raw) { return raw <= static_cast<uint8_t>(ZoneParam::kTurnOnVolume); }


}  // namespace

bool split_set_data(const Frame& f, SetDataView& v) {
  if (f.type != static_cast<uint8_t>(MsgType::kSetData)) {
    return false;
  }
  const uint8_t* p = f.body;
  size_t left = f.body_len;
  auto take = [&](size_t n, const uint8_t*& at) {
    if (left < n) {
      return false;
    }
    at = p;
    p += n;
    left -= n;
    return true;
  };
  const uint8_t* count = nullptr;
  if (!take(1, count) || !take(count[0], v.target_path)) {
    return false;
  }
  v.target_path_len = count[0];
  if (!take(1, count) || !take(count[0], v.source_path)) {
    return false;
  }
  v.source_path_len = count[0];
  const uint8_t* header = nullptr;  // packet number (2), packet count (2), data length (2)
  if (!take(6, header)) {
    return false;
  }
  v.data_len = static_cast<size_t>(header[4]) | (static_cast<size_t>(header[5]) << 8);
  v.data = p;
  return v.data_len <= left;
}

bool build_zone_power(uint8_t zone, bool on, WireFrame& out, PowerSourceZone source_zone) {
  if (!valid_zone(zone)) {
    return fail(out);
  }
  const uint8_t source_zone_id = source_zone == PowerSourceZone::kZone ? wire_zone(zone) : 0x00;
  return build_event(kController, bridge_id(source_zone_id), kPathRunMenu, event_id::kZoneOnOff,
                     on ? 1 : 0, wire_zone(zone), out);
}

bool build_all_zones_power(bool on, WireFrame& out) {
  return build_event(kAllControllers, bridge_id(0x00), kPathRunMenu, event_id::kAllOnOff,
                     on ? 1 : 0, 0, out);
}

bool build_select_source(uint8_t zone, uint8_t source, WireFrame& out) {
  if (!valid_zone(zone) || source < 1 || source > kNumSources) {
    return fail(out);
  }
  return build_event(kController, bridge_id(wire_zone(zone)), kPathSource,
                     event_id::kSelectSource, 0, static_cast<uint16_t>(source - 1), out);
}

bool build_set_volume(uint8_t zone, uint8_t volume, WireFrame& out) {
  if (!valid_zone(zone) || volume > kMaxVolume) {
    return fail(out);
  }
  return build_event(kController, bridge_id(wire_zone(zone)), kPathRunMenu, event_id::kSetVolume,
                     volume, wire_zone(zone), out);
}

bool build_keypad_event(uint8_t zone, uint16_t event, WireFrame& out) {
  if (!valid_zone(zone)) {
    return fail(out);
  }
  return build_event(kController, bridge_id(wire_zone(zone)), kPathRunMenu, event, 0, 0, out);
}

bool build_remote_key(uint8_t zone, uint8_t keycode, WireFrame& out) {
  if (!valid_zone(zone)) {
    return fail(out);
  }
  return build_event(kController, bridge_id(wire_zone(zone)), kPathRunMenu, event_id::kRemoteKey,
                     0, keycode, out);
}

bool build_set_zone_param(uint8_t zone, ZoneParam param, int8_t value, WireFrame& out) {
  uint8_t wire = 0;
  if (!valid_zone(zone) || !param_to_wire(param, value, wire)) {
    return fail(out);
  }
  const uint8_t zz = wire_zone(zone);
  FrameBuilder b(kController, bridge_id(zz), static_cast<uint8_t>(MsgType::kSetData));
  const uint8_t path[] = {0x05, 0x02, 0x00, zz, 0x00, static_cast<uint8_t>(param)};
  b.put(path, sizeof(path));
  b.put(0x00);                                  // source path: none
  b.put_u16(0).put_u16(1).put_u16(1).put(wire);  // packet 0 of 1, 1 data byte
  return b.finish(out);
}

bool build_get_zone_param(uint8_t zone, ZoneParam param, WireFrame& out) {
  if (!valid_zone(zone)) {
    return fail(out);
  }
  const uint8_t zz = wire_zone(zone);
  FrameBuilder b(kController, bridge_id(zz), static_cast<uint8_t>(MsgType::kRequestData));
  const uint8_t body[] = {0x05, 0x02, 0x00, zz, 0x00, static_cast<uint8_t>(param), 0x00, 0x00};
  b.put(body, sizeof(body));
  return b.finish(out);
}

bool build_get_all_zone_info(uint8_t zone, WireFrame& out) {
  if (!valid_zone(zone)) {
    return fail(out);
  }
  const uint8_t zz = wire_zone(zone);
  FrameBuilder b(kController, bridge_id(zz), static_cast<uint8_t>(MsgType::kRequestData));
  const uint8_t body[] = {0x04, 0x02, 0x00, zz, kZoneInfoBranch, 0x00, 0x00};
  b.put(body, sizeof(body));
  return b.finish(out);
}

bool build_session_start(SessionStartEvent event, WireFrame& out) {
  switch (event) {
    case SessionStartEvent::kDeviceRestart:
      return build_event(kController, bridge_id(0x00), kPathRunMenu, event_id::kDeviceRestart,
                         0x0003, 0x0035, out);
    case SessionStartEvent::kNone:
      break;
  }
  return fail(out);
}

bool build_data_handshake(uint8_t zone, uint8_t handshake_type, WireFrame& out) {
  if (!valid_zone(zone)) {
    return fail(out);
  }
  return build_handshake(kController, bridge_id(wire_zone(zone)), handshake_type, out);
}

bool build_handshake(const DeviceId& target, const DeviceId& source, uint8_t handshake_type,
                     WireFrame& out) {
  FrameBuilder b(target, source, static_cast<uint8_t>(MsgType::kHandshake));
  b.put(handshake_type);
  return b.finish(out);
}

ReplyResult parse_all_zone_info(const Frame& frame, ZoneInfo& out) {
  SetDataView v{};
  if (!split_set_data(frame, v) || v.source_path_len != 4 || v.source_path[0] != 0x02 ||
      v.source_path[1] != 0x00 || v.source_path[3] != kZoneInfoBranch) {
    return ReplyResult::kNotThisMessage;
  }
  const uint8_t zz = v.source_path[2];
  if (zz >= kNumZones || v.data_len < kZoneInfoFields) {
    return ReplyResult::kInvalid;
  }
  const uint8_t* d = v.data;
  int8_t bass = 0;
  int8_t treble = 0;
  int8_t balance = 0;
  if (d[0] > 1 || d[1] > 6 || d[2] > kMaxVolume || d[5] > 1 || d[7] > 1 || d[8] > 1 ||
      !param_from_wire(ZoneParam::kBass, d[3], bass) ||
      !param_from_wire(ZoneParam::kTreble, d[4], treble) ||
      !param_from_wire(ZoneParam::kBalance, d[6], balance)) {
    return ReplyResult::kInvalid;
  }
  out.zone = static_cast<uint8_t>(zz + 1);
  out.power = d[0] == 1;
  out.source = static_cast<uint8_t>(d[1] + 1);
  out.volume = d[2];
  out.bass = bass;
  out.treble = treble;
  out.loudness = d[5] == 1;
  out.balance = balance;
  out.system_on = d[7] == 1;
  out.shared_source = d[8] == 1;
  // d[9] party mode and d[10] DND are not supported on the CAA6.6 (SPEC §3.4): ignored.
  return ReplyResult::kOk;
}

ReplyResult parse_zone_param(const Frame& frame, ZoneParamValue& out) {
  SetDataView v{};
  if (!split_set_data(frame, v) || v.source_path_len != 5 || v.source_path[0] != 0x02 ||
      v.source_path[1] != 0x00 || v.source_path[3] != 0x00 || !valid_param(v.source_path[4])) {
    return ReplyResult::kNotThisMessage;
  }
  const uint8_t zz = v.source_path[2];
  const auto param = static_cast<ZoneParam>(v.source_path[4]);
  int8_t value = 0;
  if (zz >= kNumZones || v.data_len < 1 || !param_from_wire(param, v.data[0], value)) {
    return ReplyResult::kInvalid;
  }
  out.zone = static_cast<uint8_t>(zz + 1);
  out.param = param;
  out.value = value;
  return ReplyResult::kOk;
}

ReplyResult parse_handshake(const Frame& frame, uint8_t& handshake_type) {
  if (frame.type != static_cast<uint8_t>(MsgType::kHandshake)) {
    return ReplyResult::kNotThisMessage;
  }
  if (frame.body_len < 1) {
    return ReplyResult::kInvalid;
  }
  handshake_type = frame.body[0];
  return ReplyResult::kOk;
}

}  // namespace rnet
