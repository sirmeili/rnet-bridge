#include "zone_model.h"

namespace rnet {

namespace {

bool valid_zone(uint8_t zone) { return zone >= 1 && zone <= kNumZones; }

uint8_t clamp_volume(int v) {
  if (v < 0) {
    return 0;
  }
  if (v > kMaxVolume) {
    return kMaxVolume;
  }
  return static_cast<uint8_t>(v);
}

}  // namespace

ZoneModel::ZoneModel() : zones_{}, stored_volume_{}, commanded_valid_{}, commanded_volume_{} {}

void ZoneModel::apply(const ZoneInfo& info) {
  if (!valid_zone(info.zone)) {
    return;
  }
  const size_t i = info.zone - 1;
  ZoneState next = zones_[i];
  next.valid = true;
  next.power = info.power;
  next.source = info.source;
  next.volume = info.volume;
  next.bass = info.bass;
  next.treble = info.treble;
  next.balance = info.balance;
  next.loudness = info.loudness;
  next.system_on = info.system_on;
  next.shared_source = info.shared_source;
  if (next.muted && info.volume != 0) {
    next.muted = false;
  }
  commanded_valid_[i] = false;

  const ZoneState& cur = zones_[i];
  if (!cur.valid || cur.power != next.power || cur.source != next.source ||
      cur.volume != next.volume || cur.muted != next.muted || cur.bass != next.bass ||
      cur.treble != next.treble || cur.balance != next.balance || cur.loudness != next.loudness ||
      cur.system_on != next.system_on || cur.shared_source != next.shared_source) {
    zones_[i] = next;
    mark(info.zone);
  }
}

void ZoneModel::apply_turn_on_volume(uint8_t zone, uint8_t value) {
  if (!valid_zone(zone) || value > kMaxVolume) {
    return;
  }
  ZoneState& z = zones_[zone - 1];
  if (!z.turn_on_volume_valid || z.turn_on_volume != value) {
    z.turn_on_volume_valid = true;
    z.turn_on_volume = value;
    mark(zone);
  }
}

bool ZoneModel::mute(uint8_t zone, uint8_t& volume) {
  if (!valid_zone(zone) || !zones_[zone - 1].valid) {
    return false;
  }
  ZoneState& z = zones_[zone - 1];
  if (!z.muted) {
    // Muting while already muted must not overwrite the stored volume with 0.
    stored_volume_[zone - 1] = commanded_valid_[zone - 1] ? commanded_volume_[zone - 1] : z.volume;
    z.muted = true;
    mark(zone);
  }
  volume = 0;
  return true;
}

bool ZoneModel::unmute(uint8_t zone, uint8_t& volume) {
  if (!valid_zone(zone) || !zones_[zone - 1].muted) {
    return false;
  }
  ZoneState& z = zones_[zone - 1];
  const uint8_t stored = stored_volume_[zone - 1];
  volume = stored == 0 ? kUnmuteDefaultVolume : stored;
  z.muted = false;
  mark(zone);
  return true;
}

bool ZoneModel::step_volume(uint8_t zone, int delta, uint8_t& volume) {
  if (!valid_zone(zone) || !zones_[zone - 1].valid) {
    return false;
  }
  const size_t i = zone - 1;
  ZoneState& z = zones_[i];
  int base = z.volume;
  if (z.muted) {
    base = stored_volume_[i];
    z.muted = false;
    mark(zone);
  } else if (commanded_valid_[i]) {
    base = commanded_volume_[i];
  }
  volume = clamp_volume(base + delta);
  return true;
}

void ZoneModel::note_commanded_volume(uint8_t zone, uint8_t volume) {
  if (!valid_zone(zone)) {
    return;
  }
  commanded_valid_[zone - 1] = true;
  commanded_volume_[zone - 1] = volume;
}

uint8_t ZoneModel::take_changed() {
  const uint8_t c = changed_;
  changed_ = 0;
  return c;
}

}  // namespace rnet
