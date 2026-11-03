#include "platform/log.h"

#include <cstdarg>
#include <cstdio>

void Log::printf(bridge::LogLevel level, const char* fmt, ...) {
  if (lock_ == nullptr) {
    lock_ = xSemaphoreCreateMutex();  // first call is in setup(), before other tasks log
  }
  xSemaphoreTake(lock_, portMAX_DELAY);
  Line& l = lines_[next_seq_ % kLines];
  l.seq = next_seq_;
  l.level = level;
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(l.text, sizeof(l.text), fmt, args);
  va_end(args);
  usb_.printf("[%8lu] %s: %s\r\n", static_cast<unsigned long>(millis()),
              bridge::log_level_name(level), l.text);
  next_seq_ = next_seq_ + 1;  // publish the line last
  xSemaphoreGive(lock_);
}

const Log::Line* Log::line(uint32_t seq) const {
  if (seq >= next_seq_ || next_seq_ - seq > kLines) {
    return nullptr;
  }
  return &lines_[seq % kLines];
}
