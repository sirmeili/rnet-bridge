// Six-zone state cache with change detection and mute emulation (SPEC §4.3).
// Pure C++17, no Arduino dependencies, no heap allocation. Zones are 1-based.
#pragma once

#include <cstdint>

#include "rnet_messages.h"

namespace rnet {

struct ZoneState {
  bool valid;  // false until the first successful poll of this zone
  bool power;
  uint8_t source;  // 1-based. Only meaningful while the zone is on (SPEC §3.4)
  uint8_t volume;  // 0..50, as last reported by the controller
  bool muted;      // emulated, RAM only
  int8_t bass;     // -10..10
  int8_t treble;   // -10..10
  int8_t balance;  // -10..10, negative = left
  bool loudness;
  bool turn_on_volume_valid;  // false until the first turn-on volume reply
  uint8_t turn_on_volume;     // 0..50
  bool system_on;
  bool shared_source;
};

class ZoneModel {
 public:
  // Volume restored by unmute when the volume stored at mute time was 0 (SPEC §4.3).
  static constexpr uint8_t kUnmuteDefaultVolume = 10;

  ZoneModel();

  // `zone` must be 1..kNumZones.
  const ZoneState& zone(uint8_t zone) const { return zones_[zone - 1]; }

  // Applies a poll result. A volume other than 0 while muted means someone changed the volume at a
  // keypad, so mute is cleared.
  void apply(const ZoneInfo& info);
  void apply_turn_on_volume(uint8_t zone, uint8_t value);

  // Mute emulation. Each returns false if there is nothing to send; otherwise `volume` is the value
  // to send with Set Volume. Mute and volume steps need a valid zone.
  bool mute(uint8_t zone, uint8_t& volume);
  bool unmute(uint8_t zone, uint8_t& volume);
  // Volume up/down by `delta` raw units, clamped to 0..50. Steps from the volume stored at mute time
  // when muted, and unmutes. Steps from the last commanded volume until a poll confirms it, so
  // repeated steps don't all start from the same stale value.
  bool step_volume(uint8_t zone, int delta, uint8_t& volume);
  // Records a Set Volume sent by the engine, as the base for the next step.
  void note_commanded_volume(uint8_t zone, uint8_t volume);
  // The zone's current volume: the last commanded one until a poll confirms it, else the
  // reported one. `zone` must be 1..kNumZones.
  uint8_t current_volume(uint8_t zone) const {
    return commanded_valid_[zone - 1] ? commanded_volume_[zone - 1] : zones_[zone - 1].volume;
  }

  // Bit (zone - 1) is set when that zone's state changed since the last take_changed().
  uint8_t take_changed();

 private:
  void mark(uint8_t zone) { changed_ = static_cast<uint8_t>(changed_ | (1u << (zone - 1))); }

  ZoneState zones_[kNumZones];
  uint8_t stored_volume_[kNumZones];     // volume at mute time
  bool commanded_valid_[kNumZones];      // a Set Volume was sent and not yet confirmed by a poll
  uint8_t commanded_volume_[kNumZones];
  uint8_t changed_ = 0;
};

}  // namespace rnet
