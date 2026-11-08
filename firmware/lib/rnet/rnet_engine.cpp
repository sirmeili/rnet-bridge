#include "rnet_engine.h"

namespace rnet {

namespace {

bool valid_zone(uint8_t zone) { return zone >= 1 && zone <= kNumZones; }

// True if `now` is at or after `at` (wrap-safe).
bool reached(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

bool same_bytes(const uint8_t* a, size_t a_len, const uint8_t* b, size_t b_len) {
  if (a_len != b_len) {
    return false;
  }
  for (size_t i = 0; i < a_len; ++i) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

// Validates a command by building the frame it would send. Volume steps and mute need a polled
// zone, which is checked when they are sent, not here.
bool valid_command(const Command& c, const EngineConfig& cfg) {
  WireFrame f{};
  switch (c.type) {
    case CommandType::kSetPower:
      return (c.value == 0 || c.value == 1) &&
             build_zone_power(c.zone, c.value == 1, f, cfg.power_source_zone);
    case CommandType::kSetSource:
      return c.value >= 1 && c.value <= kNumSources &&
             build_select_source(c.zone, static_cast<uint8_t>(c.value), f);
    case CommandType::kSetVolume:
      return c.value >= 0 && c.value <= kMaxVolume &&
             build_set_volume(c.zone, static_cast<uint8_t>(c.value), f);
    case CommandType::kVolumeUp:
    case CommandType::kVolumeDown:
      return valid_zone(c.zone);
    case CommandType::kSetMute:
      return valid_zone(c.zone) && (c.value == 0 || c.value == 1);
    case CommandType::kSetParam:
      return c.value >= -128 && c.value <= 127 &&
             build_set_zone_param(c.zone, c.param, static_cast<int8_t>(c.value), f);
    case CommandType::kAllOn:
    case CommandType::kAllOff:
      return true;
    case CommandType::kRefresh:
      return c.zone == 0 || valid_zone(c.zone);
    case CommandType::kRaw:
      return false;  // only through submit_raw()
  }
  return false;
}

}  // namespace

// Turn-on volumes are first polled when the link comes up (set_link), so a missing controller at
// boot doesn't delay the zone polls that detect it.
Engine::Engine(ByteIo& io, ZoneModel& model, const EngineConfig& config)
    : io_(io), model_(model), cfg_(config), queue_{}, results_{} {}

CommandError Engine::submit(const Command& cmd) {
  if (!valid_command(cmd, cfg_)) {
    return CommandError::kInvalid;
  }
  if (queue_count_ == kQueueDepth) {
    return CommandError::kBusy;
  }
  queue_[(queue_head_ + queue_count_) % kQueueDepth] = cmd;
  ++queue_count_;
  return CommandError::kNone;
}

CommandError Engine::submit_raw(uint32_t id, const uint8_t* wire, size_t len) {
  if (len == 0 || len > kMaxWireBytes) {
    return CommandError::kInvalid;
  }
  if (queue_count_ == kQueueDepth) {
    return CommandError::kBusy;
  }
  for (size_t i = 0; i < kRawSlots; ++i) {
    if (!raw_[i].used) {
      raw_[i].used = true;
      for (size_t k = 0; k < len; ++k) {
        raw_[i].frame.bytes[k] = wire[k];
      }
      raw_[i].frame.len = len;
      queue_[(queue_head_ + queue_count_) % kQueueDepth] =
          Command{id, CommandType::kRaw, 0, static_cast<int16_t>(i), ZoneParam::kBass};
      ++queue_count_;
      return CommandError::kNone;
    }
  }
  return CommandError::kBusy;
}

bool Engine::next_result(CommandResult& out) {
  if (results_count_ == 0) {
    return false;
  }
  out = results_[results_head_];
  results_head_ = (results_head_ + 1) % kQueueDepth;
  --results_count_;
  return true;
}

bool Engine::take_link_changed() {
  const bool c = link_changed_;
  link_changed_ = false;
  return c;
}

void Engine::tick(uint32_t now_ms) {
  read_bytes(now_ms);
  if (parser_.in_frame() && now_ms - last_rx_byte_ms_ >= cfg_.inter_byte_timeout_ms) {
    parser_.reset();
    ++stats_.dropped;
  }
  check_timeout(now_ms);
  const bool gap_ok = !sent_any_ || now_ms - last_tx_ms_ >= cfg_.min_frame_gap_ms;
  if (outstanding_.kind == RequestKind::kNone && gap_ok) {
    send_next(now_ms);
  }
}

void Engine::read_bytes(uint32_t now_ms) {
  for (int c = io_.read(); c >= 0; c = io_.read()) {
    last_rx_byte_ms_ = now_ms;
    switch (parser_.feed(static_cast<uint8_t>(c))) {
      case ParseResult::kFrame:
        on_frame(now_ms);
        break;
      case ParseResult::kChecksumError:
        ++stats_.checksum_errors;
        break;
      case ParseResult::kResync:
      case ParseResult::kOverflow:
      case ParseResult::kMalformed:
        ++stats_.dropped;
        break;
      case ParseResult::kNone:
        break;
    }
  }
}

void Engine::on_frame(uint32_t now_ms) {
  ++stats_.rx;
  if (tap_ != nullptr) {
    tap_(tap_ctx_, false, parser_.last_wire(), parser_.last_wire_len());
  }
  const Frame& f = parser_.frame();
  // The port also repeats keypad bus traffic (SPEC §3.8). Only frames addressed to us matter.
  if (f.target.keypad != kBridgeKeypadId || f.target.zone >= kNumZones) {
    return;
  }

  uint8_t hs_type = 0;
  if (parse_handshake(f, hs_type) == ReplyResult::kOk) {
    // The controller ACKing our Set Data (SPEC §3.5): proof that it hears us.
    ++stats_.handshakes_rx;
    consecutive_timeouts_ = 0;
    set_link(true, now_ms);
    return;
  }
  if (f.type != static_cast<uint8_t>(MsgType::kSetData)) {
    return;
  }

  ZoneInfo info{};
  ZoneParamValue param{};
  if (parse_all_zone_info(f, info) == ReplyResult::kOk) {
    model_.apply(info);
    on_reply_to_us(RequestKind::kZoneInfo, info.zone, now_ms);
  } else if (parse_zone_param(f, param) == ReplyResult::kOk) {
    if (param.param == ZoneParam::kTurnOnVolume) {
      model_.apply_turn_on_volume(param.zone, static_cast<uint8_t>(param.value));
      on_reply_to_us(RequestKind::kTurnOnVolume, param.zone, now_ms);
    }
  }

  if (cfg_.send_data_ack) {
    WireFrame ack{};
    if (build_data_handshake(static_cast<uint8_t>(f.target.zone + 1), cfg_.data_handshake_type,
                             ack)) {
      io_.write(ack.bytes, ack.len);
      ++stats_.tx;
      ++stats_.acks_tx;
      if (tap_ != nullptr) {
        tap_(tap_ctx_, true, ack.bytes, ack.len);
      }
    }
  }
}

void Engine::on_reply_to_us(RequestKind kind, uint8_t zone, uint32_t now_ms) {
  const bool expected = outstanding_.kind == kind && outstanding_.zone == zone;
  // A reply nobody asked for that repeats the last one is a retransmission (SPEC §3.5), unless a
  // raw frame (which may have been a request) went out just before.
  const bool after_raw = raw_sent_ && now_ms - raw_sent_ms_ < cfg_.request_timeout_ms;
  // A late answer to another attempt of a retried request is not a retransmission either.
  const bool late = late_.kind == kind && late_.zone == zone &&
                    static_cast<int32_t>(late_.until_ms - now_ms) > 0;
  if (remember_reply(kind, zone) && !expected && !after_raw && !late) {
    ++stats_.duplicate_replies;
  }
  consecutive_timeouts_ = 0;
  set_link(true, now_ms);
  if (expected) {
    if (outstanding_.attempts > 1) {
      late_ = {kind, zone, now_ms + cfg_.request_timeout_ms};
    }
    outstanding_.kind = RequestKind::kNone;
    if (kind == RequestKind::kTurnOnVolume) {
      schedule_turn_on_volume(zone, now_ms + cfg_.turn_on_volume_poll_ms);
    }
  }
}

bool Engine::remember_reply(RequestKind kind, uint8_t zone) {
  LastReply& last = kind == RequestKind::kZoneInfo ? last_zone_info_[zone - 1] : last_tov_[zone - 1];
  const uint8_t* wire = parser_.last_wire();
  const size_t len = parser_.last_wire_len();
  const bool same = same_bytes(last.bytes, last.len, wire, len);
  for (size_t i = 0; i < len && i < kMaxWireBytes; ++i) {
    last.bytes[i] = wire[i];
  }
  last.len = len;
  return same;
}

void Engine::check_timeout(uint32_t now_ms) {
  if (outstanding_.kind == RequestKind::kNone ||
      now_ms - outstanding_.sent_ms < cfg_.request_timeout_ms) {
    return;
  }
  ++stats_.timeouts;
  if (consecutive_timeouts_ < 255) {
    ++consecutive_timeouts_;
  }
  if (consecutive_timeouts_ >= cfg_.link_down_timeouts) {
    set_link(false, now_ms);
  }
  if (outstanding_.attempts <= cfg_.request_retries) {
    const RequestKind kind = outstanding_.kind;
    const uint8_t zone = outstanding_.zone;
    const uint8_t attempts = outstanding_.attempts;
    ++stats_.retries;
    send_request(kind, zone, now_ms);
    outstanding_.attempts = static_cast<uint8_t>(attempts + 1);
  } else {
    request_failed(now_ms);
  }
}

void Engine::request_failed(uint32_t now_ms) {
  if (outstanding_.kind == RequestKind::kTurnOnVolume) {
    schedule_turn_on_volume(outstanding_.zone, now_ms + cfg_.turn_on_volume_poll_ms);
  }
  late_ = {outstanding_.kind, outstanding_.zone, now_ms + cfg_.request_timeout_ms};
  outstanding_.kind = RequestKind::kNone;
}

void Engine::set_link(bool up, uint32_t now_ms) {
  if (up == link_up_) {
    return;
  }
  link_up_ = up;
  link_changed_ = true;
  if (up) {
    // Session start (SPEC §3.6): send the configured event, and refresh turn-on volumes.
    session_start_pending_ = cfg_.session_start != SessionStartEvent::kNone;
    schedule_turn_on_volume(0, now_ms);
  }
}

bool Engine::send_next(uint32_t now_ms) {
  if (session_start_pending_) {
    session_start_pending_ = false;
    WireFrame f{};
    if (build_session_start(cfg_.session_start, f)) {
      transmit(f, now_ms);
      return true;
    }
  }

  // A volume restore follows its turn-on volume change before anything else, so the level
  // change is as short as possible.
  for (uint8_t z = 1; z <= kNumZones; ++z) {
    if (restore_due_[z - 1]) {
      restore_due_[z - 1] = false;
      WireFrame f{};
      if (link_up_ && build_set_volume(z, restore_volume_[z - 1], f)) {
        transmit(f, now_ms);
        model_.note_commanded_volume(z, restore_volume_[z - 1]);
        schedule_priority_poll(z, now_ms);
        return true;
      }
    }
  }

  // Commands jump ahead of polls (SPEC §3.8). Some complete without sending anything.
  while (queue_count_ > 0) {
    const Command cmd = queue_[queue_head_];
    queue_head_ = (queue_head_ + 1) % kQueueDepth;
    --queue_count_;
    if (send_command(cmd, now_ms)) {
      return true;
    }
  }

  for (uint8_t z = 1; z <= kNumZones; ++z) {
    if (priority_due_[z - 1] && reached(now_ms, priority_at_ms_[z - 1])) {
      priority_due_[z - 1] = false;
      return send_request(RequestKind::kZoneInfo, z, now_ms);
    }
  }
  for (uint8_t z = 1; z <= kNumZones; ++z) {
    if (tov_due_[z - 1] && reached(now_ms, tov_at_ms_[z - 1])) {
      tov_due_[z - 1] = false;
      return send_request(RequestKind::kTurnOnVolume, z, now_ms);
    }
  }
  if (!round_robin_started_ || now_ms - last_round_robin_ms_ >= cfg_.poll_interval_ms) {
    round_robin_started_ = true;
    last_round_robin_ms_ = now_ms;
    const uint8_t z = next_poll_zone_;
    next_poll_zone_ = static_cast<uint8_t>(z % kNumZones + 1);
    return send_request(RequestKind::kZoneInfo, z, now_ms);
  }
  return false;
}

bool Engine::send_command(const Command& c, uint32_t now_ms) {
  if (c.type == CommandType::kRaw) {
    RawSlot& slot = raw_[static_cast<size_t>(c.value)];
    slot.used = false;
    if (!link_up_) {
      finish(c.id, CommandError::kLinkDown);
      return false;
    }
    transmit(slot.frame, now_ms);
    raw_sent_ = true;
    raw_sent_ms_ = now_ms;
    finish(c.id, CommandError::kNone);
    return true;
  }
  if (!link_up_) {
    finish(c.id, CommandError::kLinkDown);
    return false;
  }
  WireFrame f{};
  bool built = false;
  uint8_t volume = 0;
  switch (c.type) {
    case CommandType::kSetPower:
      built = build_zone_power(c.zone, c.value == 1, f, cfg_.power_source_zone);
      break;
    case CommandType::kSetSource:
      built = build_select_source(c.zone, static_cast<uint8_t>(c.value), f);
      break;
    case CommandType::kSetVolume:
      volume = static_cast<uint8_t>(c.value);
      built = build_set_volume(c.zone, volume, f);
      break;
    case CommandType::kVolumeUp:
    case CommandType::kVolumeDown:
      if (!model_.step_volume(c.zone, c.type == CommandType::kVolumeUp ? 1 : -1, volume)) {
        finish(c.id, CommandError::kInvalid);
        return false;
      }
      built = build_set_volume(c.zone, volume, f);
      break;
    case CommandType::kSetMute:
      if (c.value == 1) {
        if (!model_.mute(c.zone, volume)) {
          finish(c.id, CommandError::kInvalid);
          return false;
        }
      } else if (!model_.unmute(c.zone, volume)) {
        finish(c.id, CommandError::kNone);  // not muted: nothing to do
        return false;
      }
      built = build_set_volume(c.zone, volume, f);
      break;
    case CommandType::kSetParam:
      built = build_set_zone_param(c.zone, c.param, static_cast<int8_t>(c.value), f);
      if (built && c.param == ZoneParam::kTurnOnVolume) {
        schedule_turn_on_volume(c.zone, now_ms + cfg_.priority_poll_delay_ms);
        const ZoneState& z = model_.zone(c.zone);
        if (cfg_.restore_volume_after_turn_on_volume && z.valid && z.power) {
          restore_due_[c.zone - 1] = true;
          restore_volume_[c.zone - 1] = model_.current_volume(c.zone);
        }
      }
      break;
    case CommandType::kAllOn:
    case CommandType::kAllOff:
      built = build_all_zones_power(c.type == CommandType::kAllOn, f);
      break;
    case CommandType::kRefresh:
      schedule_priority_poll(c.zone, now_ms);
      schedule_turn_on_volume(c.zone, now_ms);
      finish(c.id, CommandError::kNone);
      return false;
    case CommandType::kRaw:
      break;  // handled above
  }
  if (!built) {
    finish(c.id, CommandError::kInvalid);
    return false;
  }
  transmit(f, now_ms);
  if (c.type == CommandType::kSetVolume || c.type == CommandType::kVolumeUp ||
      c.type == CommandType::kVolumeDown || c.type == CommandType::kSetMute) {
    model_.note_commanded_volume(c.zone, volume);
  }
  const bool all = c.type == CommandType::kAllOn || c.type == CommandType::kAllOff;
  schedule_priority_poll(all ? 0 : c.zone, now_ms);
  finish(c.id, CommandError::kNone);
  return true;
}

bool Engine::send_request(RequestKind kind, uint8_t zone, uint32_t now_ms) {
  WireFrame f{};
  const bool built = kind == RequestKind::kZoneInfo
                         ? build_get_all_zone_info(zone, f)
                         : build_get_zone_param(zone, ZoneParam::kTurnOnVolume, f);
  if (!built) {
    return false;
  }
  transmit(f, now_ms);
  outstanding_ = {kind, zone, 1, now_ms};
  return true;
}

void Engine::transmit(const WireFrame& frame, uint32_t now_ms) {
  io_.write(frame.bytes, frame.len);
  ++stats_.tx;
  last_tx_ms_ = now_ms;
  sent_any_ = true;
  if (tap_ != nullptr) {
    tap_(tap_ctx_, true, frame.bytes, frame.len);
  }
}

void Engine::finish(uint32_t id, CommandError error) {
  if (results_count_ == kQueueDepth) {  // caller isn't draining: drop the oldest
    results_head_ = (results_head_ + 1) % kQueueDepth;
    --results_count_;
  }
  results_[(results_head_ + results_count_) % kQueueDepth] = {id, error};
  ++results_count_;
}

void Engine::schedule_priority_poll(uint8_t zone, uint32_t now_ms) {
  const uint32_t at = now_ms + cfg_.priority_poll_delay_ms;
  for (uint8_t z = 1; z <= kNumZones; ++z) {
    if (zone == 0 || zone == z) {
      priority_due_[z - 1] = true;
      priority_at_ms_[z - 1] = at;
    }
  }
}

void Engine::schedule_turn_on_volume(uint8_t zone, uint32_t at_ms) {
  for (uint8_t z = 1; z <= kNumZones; ++z) {
    if (zone == 0 || zone == z) {
      tov_due_[z - 1] = true;
      tov_at_ms_[z - 1] = at_ms;
    }
  }
}

}  // namespace rnet
