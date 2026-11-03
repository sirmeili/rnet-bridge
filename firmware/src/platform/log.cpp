#include "platform/log.h"

#include <cstdarg>
#include <cstdio>

void Log::printf(bridge::LogLevel level, const char* fmt, ...) {
  Line& l = lines_[next_seq_ % kLines];
  l.seq = next_seq_++;
  l.level = level;
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(l.text, sizeof(l.text), fmt, args);
  va_end(args);
  usb_.printf("[%8lu] %s: %s\r\n", static_cast<unsigned long>(millis()),
              bridge::log_level_name(level), l.text);
}

const Log::Line* Log::line(uint32_t seq) const {
  if (seq >= next_seq_ || next_seq_ - seq > kLines) {
    return nullptr;
  }
  return &lines_[seq % kLines];
}
