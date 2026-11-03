// Log ring buffer with level filtering (SPEC §4.2). Every line goes to USB serial; the API server
// reads new lines from the ring and forwards them to subscribed WebSocket clients. Call from the
// main loop task only.
#pragma once

#include <Arduino.h>

#include "api_protocol.h"

class Log {
 public:
  static constexpr size_t kLines = 32;
  static constexpr size_t kLineMax = 120;

  struct Line {
    uint32_t seq;
    bridge::LogLevel level;
    char text[kLineMax];
  };

  explicit Log(Print& usb) : usb_(usb) {}

  void printf(bridge::LogLevel level, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

  // Sequence number of the next line to be written; lines are numbered from 0.
  uint32_t next_seq() const { return next_seq_; }
  // The line with sequence `seq`, or nullptr if it was overwritten or not written yet.
  const Line* line(uint32_t seq) const;

 private:
  Print& usb_;
  Line lines_[kLines] = {};
  uint32_t next_seq_ = 0;
};

extern Log g_log;

#define LOG_DEBUG(...) g_log.printf(bridge::LogLevel::kDebug, __VA_ARGS__)
#define LOG_INFO(...) g_log.printf(bridge::LogLevel::kInfo, __VA_ARGS__)
#define LOG_WARN(...) g_log.printf(bridge::LogLevel::kWarn, __VA_ARGS__)
#define LOG_ERROR(...) g_log.printf(bridge::LogLevel::kError, __VA_ARGS__)
