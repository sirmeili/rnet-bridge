#include "rnet_duplicate.h"

namespace rnet {

namespace {

bool same(const uint8_t* a, size_t a_len, const uint8_t* b, size_t b_len) {
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

}  // namespace

bool DuplicateDetector::on_receive(const uint8_t* wire, size_t len, uint32_t now_ms,
                                   uint32_t& age_ms) {
  if (len > kMaxWireBytes) {
    return false;
  }
  bool duplicate = false;
  for (size_t i = 0; i < count_; ++i) {
    if (same(entries_[i].bytes, entries_[i].len, wire, len)) {
      age_ms = now_ms - entries_[i].at_ms;  // unsigned wrap-safe
      duplicate = true;
      break;
    }
  }
  if (count_ == 0) {
    next_ = 0;
  }
  Entry& e = entries_[next_];
  for (size_t i = 0; i < len; ++i) {
    e.bytes[i] = wire[i];
  }
  e.len = len;
  e.at_ms = now_ms;
  next_ = (next_ + 1) % kHistory;
  if (count_ < kHistory) {
    ++count_;
  }
  return duplicate;
}

}  // namespace rnet
