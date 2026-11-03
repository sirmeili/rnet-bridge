// M2 "first contact" serial console (SPEC §9 M2). Commands typed over USB serial build and send
// RNET frames; every received frame is hex-dumped and decoded. Settings for the unverified protocol
// details (ACK on/off and type, Zone On/Off source zone) can be changed at runtime, so the SPEC §11
// checks run without reflashing. Type `help` for the command list.
//
// This is a bring-up tool, not the engine (M3): it sends exactly what you type, with no queue, retries
// or polling.
#pragma once

#include <Arduino.h>

#include "rnet_codec.h"
#include "rnet_duplicate.h"
#include "rnet_messages.h"

class Console {
 public:
  Console(Print& out, Stream& in, HardwareSerial& rnet);

  void begin();
  // Call every loop(). Never blocks waiting for input.
  void poll(uint32_t now_ms);

 private:
  static constexpr size_t kLineMax = 160;
  static constexpr size_t kLoopbackBytes = 64;
  static constexpr uint32_t kLoopbackTimeoutMs = 500;

  struct Stats {
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t checksum_errors;
    uint32_t dropped;  // resync, overflow, malformed
    uint32_t timeouts;
    uint32_t duplicates;
    uint32_t handshakes_rx;
    uint32_t acks_tx;
  };

  void read_usb();
  void read_rnet(uint32_t now_ms);
  void check_timeouts(uint32_t now_ms);
  void execute(char* line);

  void on_frame(uint32_t now_ms);
  void on_rx_error(const char* what);
  void send(const rnet::WireFrame& frame, const char* label, bool is_ack);
  void send_or_report(bool built, const rnet::WireFrame& frame, const char* label);

  void start_loopback(uint32_t now_ms);
  void finish_loopback();

  void print_help();
  void print_stats();
  void print_prompt();

  Print& out_;
  Stream& in_;
  HardwareSerial& rnet_;

  char line_[kLineMax];
  size_t line_len_ = 0;

  rnet::FrameParser parser_;
  rnet::DuplicateDetector duplicates_;
  uint32_t last_rx_byte_ms_ = 0;
  uint32_t now_ms_ = 0;
  Stats stats_{};

  // Runtime settings for SPEC §11 checks.
  bool auto_ack_ = false;
  uint8_t ack_type_;
  rnet::PowerSourceZone power_source_zone_ = rnet::PowerSourceZone::kZero;

  bool loopback_active_ = false;
  uint8_t loopback_sent_[kLoopbackBytes];
  uint8_t loopback_got_[kLoopbackBytes];
  size_t loopback_len_ = 0;
  uint32_t loopback_deadline_ms_ = 0;
};
