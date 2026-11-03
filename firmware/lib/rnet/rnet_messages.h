// RNET message builders (SPEC §3.3) and reply parsers (SPEC §3.4).
//
// This is the only layer that converts between API numbering and wire numbering (CLAUDE.md rule 8):
// zones and sources are 1-based here and 0-based on the wire. Bass, treble and balance are signed
// -10..+10 here and 0..20 on the wire. Builders validate their arguments and return false on bad input.
#pragma once

#include <cstddef>
#include <cstdint>

#include "rnet_codec.h"

namespace rnet {

constexpr uint8_t kNumZones = 6;
constexpr uint8_t kNumSources = 6;
constexpr uint8_t kMaxVolume = 50;  // raw 0..50, keypads display x2
constexpr int8_t kMinTone = -10;    // bass, treble, balance
constexpr int8_t kMaxTone = 10;

constexpr uint8_t kBridgeKeypadId = 0x70;
constexpr DeviceId kController{0x00, 0x00, 0x7F};
constexpr DeviceId kAllControllers{0x7E, 0x00, 0x7F};

// Event IDs (16-bit, sent lo/hi). IDs > 0x7F go out escaped, e.g. 0xDC -> F1 23.
namespace event_id {
constexpr uint16_t kVolumeUp = 0x7F;
constexpr uint16_t kVolumeDown = 0x80;
constexpr uint16_t kRemoteKey = 0xBF;  // Remote Control Key Release; keycode in event data
constexpr uint16_t kSelectSource = 0xC1;
constexpr uint16_t kZoneOnOff = 0xDC;
constexpr uint16_t kAllOnOff = 0xDD;
constexpr uint16_t kSetVolume = 0xDE;
constexpr uint16_t kDeviceRestart = 0x0155;
}  // namespace event_id

enum class ZoneParam : uint8_t {
  kBass = 0x00,
  kTreble = 0x01,
  kLoudness = 0x02,
  kBalance = 0x03,
  kTurnOnVolume = 0x04,
};

// Session-start options (SPEC §3.6). Which one the CAA6.6 needs is unverified (SPEC §11).
enum class SessionStartEvent : uint8_t {
  kDeviceRestart,
  kNone,
};

// --- Builders. All return false (and out.len == 0) on invalid arguments. ---

// Source Zone ID (byte 6) used for Zone On/Off. SPEC §3.3 uses kZero (the PDF hex table form);
// kZone is the PDF §8.1.1 template form, kept so M2 can test both (SPEC §11 item 8).
enum class PowerSourceZone : uint8_t {
  kZero,
  kZone,
};

// Zone power.
bool build_zone_power(uint8_t zone, bool on, WireFrame& out,
                      PowerSourceZone source_zone = PowerSourceZone::kZero);
bool build_all_zones_power(bool on, WireFrame& out);
bool build_select_source(uint8_t zone, uint8_t source, WireFrame& out);
bool build_set_volume(uint8_t zone, uint8_t volume, WireFrame& out);
// Keypad event (PDF §6.1), e.g. event_id::kVolumeUp.
bool build_keypad_event(uint8_t zone, uint16_t event, WireFrame& out);
// Remote control key (PDF §6.2), e.g. keycode 0x20 = Menu.
bool build_remote_key(uint8_t zone, uint8_t keycode, WireFrame& out);

// Set Data for a zone parameter. `value` uses API units: signed -10..10 for bass/treble/balance,
// 0/1 for loudness, 0..50 for turn-on volume.
bool build_set_zone_param(uint8_t zone, ZoneParam param, int8_t value, WireFrame& out);
bool build_get_zone_param(uint8_t zone, ZoneParam param, WireFrame& out);
bool build_get_all_zone_info(uint8_t zone, WireFrame& out);

// Session start (SPEC §3.6). The Device Restart event is sent from source zone 00.
// Returns false for kNone, since there is nothing to send.
bool build_session_start(SessionStartEvent event, WireFrame& out);

// Data handshake (ACK) we send after a controller's Set Data reply. The handshake type byte is
// unverified (SPEC §3.5 / §11); callers pass the configured constant.
bool build_data_handshake(uint8_t zone, uint8_t handshake_type, WireFrame& out);

// Generic handshake frame: F0 <target> <source> 02 <type> cs F7.
bool build_handshake(const DeviceId& target, const DeviceId& source, uint8_t handshake_type,
                     WireFrame& out);

// --- Parsers. Matched on message type + path bytes, never on total length (SPEC §3.4). ---

struct ZoneInfo {
  uint8_t zone;    // 1-based
  bool power;
  uint8_t source;  // 1-based. 7 = AUX on controllers that report it (PDF §13)
  uint8_t volume;  // 0..50
  int8_t bass;     // -10..10
  int8_t treble;   // -10..10
  bool loudness;
  int8_t balance;  // -10..10, negative = left
  bool system_on;
  bool shared_source;
};

struct ZoneParamValue {
  uint8_t zone;  // 1-based
  ZoneParam param;
  int8_t value;  // API units, as for build_set_zone_param
};

// View of a Set Data body: target path, source path, packet number/count, data length, data.
// Pointers refer into the Frame passed to split_set_data().
struct SetDataView {
  const uint8_t* target_path;
  size_t target_path_len;
  const uint8_t* source_path;
  size_t source_path_len;
  const uint8_t* data;
  size_t data_len;
};

// False if `frame` is not Set Data or its body is truncated.
bool split_set_data(const Frame& frame, SetDataView& out);

enum class ReplyResult : uint8_t {
  kOk,
  kNotThisMessage,  // a different message type or path; try another parser
  kInvalid,         // right message, but truncated or with out-of-range values
};

ReplyResult parse_all_zone_info(const Frame& frame, ZoneInfo& out);
ReplyResult parse_zone_param(const Frame& frame, ZoneParamValue& out);
// Any handshake frame (from the controller after our Set Data, or otherwise).
ReplyResult parse_handshake(const Frame& frame, uint8_t& handshake_type);

}  // namespace rnet
