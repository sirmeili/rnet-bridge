// RNET engine (SPEC §3.5-§3.8, §4.3): command queue, one outstanding request at a time, timeouts
// and retries, link state, and the polling scheduler. Feeds poll results into a ZoneModel.
//
// Pure C++17, no Arduino dependencies, no heap allocation. Byte I/O is injected through ByteIo and
// the clock is passed to tick(), so the engine runs against a scripted fake controller in tests.
// Never blocks: call tick() from every loop().
#pragma once

#include <cstddef>
#include <cstdint>

#include "rnet_codec.h"
#include "rnet_messages.h"
#include "zone_model.h"

namespace rnet {

class ByteIo {
 public:
  virtual ~ByteIo() = default;
  // Next received byte, or -1 if none is waiting.
  virtual int read() = 0;
  // Must not block: frames are at most kMaxWireBytes and fit the UART TX buffer.
  virtual void write(const uint8_t* bytes, size_t len) = 0;
};

// Timings default to SPEC §3.7/§3.8. Firmware fills this from config.h.
struct EngineConfig {
  uint32_t request_timeout_ms = 500;
  uint8_t request_retries = 2;
  uint32_t min_frame_gap_ms = 50;
  uint8_t link_down_timeouts = 5;      // consecutive request timeouts
  uint32_t poll_interval_ms = 500;     // round robin: one zone per interval
  uint32_t priority_poll_delay_ms = 150;
  uint32_t turn_on_volume_poll_ms = 60000;
  uint32_t inter_byte_timeout_ms = 50;
  bool send_data_ack = false;          // SPEC §3.5: not needed for keypad 0x70
  uint8_t data_handshake_type = 0x02;
  SessionStartEvent session_start = SessionStartEvent::kNone;
  PowerSourceZone power_source_zone = PowerSourceZone::kZero;
  // The CAA6.6 sets a playing zone's volume to its new turn-on volume (docs/rnet-notes.md).
  // When on, a turn-on volume change on a zone that is on is followed straight away by a Set
  // Volume back to the zone's current volume.
  bool restore_volume_after_turn_on_volume = true;
};

enum class CommandType : uint8_t {
  kSetPower,     // value 0/1
  kSetSource,    // value 1..6
  kSetVolume,    // value 0..50
  kVolumeUp,
  kVolumeDown,
  kSetMute,      // value 0/1
  kSetParam,     // param + value in API units (see build_set_zone_param)
  kAllOn,        // zone ignored
  kAllOff,       // zone ignored
  kRefresh,      // zone 1..6, or 0 = all zones
  kRaw,          // debug: queued with submit_raw(), not submit()
};

struct Command {
  uint32_t id;  // echoed in the result
  CommandType type;
  uint8_t zone;  // 1-based
  int16_t value;
  ZoneParam param;  // kSetParam only
};

enum class CommandError : uint8_t {
  kNone,
  kBusy,      // queue full
  kInvalid,   // bad arguments, or a volume step/mute before the zone was first polled
  kLinkDown,  // RNET link down when the command came up for sending
};

struct CommandResult {
  uint32_t id;
  CommandError error;
};

struct EngineStats {
  uint32_t tx;
  uint32_t rx;
  uint32_t checksum_errors;
  uint32_t dropped;  // resync, overflow, malformed, inter-byte timeout
  uint32_t timeouts;
  uint32_t retries;
  uint32_t duplicate_replies;
  uint32_t handshakes_rx;  // handshakes addressed to us (the controller ACKing our Set Data)
  uint32_t acks_tx;
};

// Called for every frame sent (`tx` true) or received with a valid checksum, for frame tracing.
using FrameTap = void (*)(void* ctx, bool tx, const uint8_t* wire, size_t len);

class Engine {
 public:
  static constexpr size_t kQueueDepth = 32;
  static constexpr size_t kRawSlots = 4;  // raw frames waiting in the queue at once

  Engine(ByteIo& io, ZoneModel& model, const EngineConfig& config = EngineConfig{});

  // Queues a command. Returns kNone if accepted (its result follows from next_result()), or kBusy /
  // kInvalid immediately, in which case no result follows.
  CommandError submit(const Command& cmd);
  // Queues complete wire bytes (F0 through F7) to send as-is, in order with other commands.
  // kInvalid if `len` is 0 or over kMaxWireBytes; kBusy if the queue or the raw slots are full.
  CommandError submit_raw(uint32_t id, const uint8_t* wire, size_t len);
  void set_frame_tap(FrameTap tap, void* ctx) {
    tap_ = tap;
    tap_ctx_ = ctx;
  }
  // Pops the next completed command result. A command counts as done (kNone) once its frame is
  // sent: the controller doesn't acknowledge events, so the follow-up poll is the confirmation.
  bool next_result(CommandResult& out);

  void tick(uint32_t now_ms);

  bool link_up() const { return link_up_; }
  // True once since the last call if link_up() changed.
  bool take_link_changed();
  const EngineStats& stats() const { return stats_; }
  size_t queue_depth() const { return queue_count_; }

 private:
  enum class RequestKind : uint8_t { kNone, kZoneInfo, kTurnOnVolume };

  struct Outstanding {
    RequestKind kind;
    uint8_t zone;
    uint8_t attempts;
    uint32_t sent_ms;
  };

  // Last reply received for each zone and kind, for duplicate detection.
  struct LastReply {
    uint8_t bytes[kMaxWireBytes];
    size_t len;
  };

  void read_bytes(uint32_t now_ms);
  void on_frame(uint32_t now_ms);
  void on_reply_to_us(RequestKind kind, uint8_t zone, uint32_t now_ms);
  void check_timeout(uint32_t now_ms);
  void request_failed(uint32_t now_ms);
  void set_link(bool up, uint32_t now_ms);
  bool send_next(uint32_t now_ms);
  bool send_command(const Command& cmd, uint32_t now_ms);
  bool send_request(RequestKind kind, uint8_t zone, uint32_t now_ms);
  void transmit(const WireFrame& frame, uint32_t now_ms);
  void finish(uint32_t id, CommandError error);
  void schedule_priority_poll(uint8_t zone, uint32_t now_ms);  // 0 = all zones
  void schedule_turn_on_volume(uint8_t zone, uint32_t at_ms);  // 0 = all zones
  // Compares the frame just parsed with the last reply of this kind for this zone, then stores it.
  bool remember_reply(RequestKind kind, uint8_t zone);

  ByteIo& io_;
  ZoneModel& model_;
  EngineConfig cfg_;

  FrameParser parser_;
  uint32_t last_rx_byte_ms_ = 0;

  Command queue_[kQueueDepth];
  size_t queue_head_ = 0;
  size_t queue_count_ = 0;
  CommandResult results_[kQueueDepth];
  size_t results_head_ = 0;
  size_t results_count_ = 0;

  Outstanding outstanding_{};
  uint32_t last_tx_ms_ = 0;
  bool sent_any_ = false;
  uint8_t next_poll_zone_ = 1;
  uint32_t last_round_robin_ms_ = 0;
  bool round_robin_started_ = false;
  bool priority_due_[kNumZones] = {};
  uint32_t priority_at_ms_[kNumZones] = {};
  bool tov_due_[kNumZones] = {};
  uint32_t tov_at_ms_[kNumZones] = {};

  bool link_up_ = false;
  bool link_changed_ = false;
  bool session_start_pending_ = false;
  uint8_t consecutive_timeouts_ = 0;

  LastReply last_zone_info_[kNumZones] = {};
  LastReply last_tov_[kNumZones] = {};

  struct RawSlot {
    bool used;
    WireFrame frame;
  };
  RawSlot raw_[kRawSlots] = {};
  // Zones whose volume must be put back after a turn-on volume change (sent before polls).
  bool restore_due_[kNumZones] = {};
  uint8_t restore_volume_[kNumZones] = {};
  bool raw_sent_ = false;
  uint32_t raw_sent_ms_ = 0;
  FrameTap tap_ = nullptr;
  void* tap_ctx_ = nullptr;

  EngineStats stats_{};
};

}  // namespace rnet
