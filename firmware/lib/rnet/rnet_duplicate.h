// Duplicate-reply detection (SPEC §3.5). If the controller does not accept our ACK, it retransmits
// the same reply about 2.5 s later. A received frame counts as a duplicate when its wire bytes match
// a frame received since our last transmission. Repeated polls that legitimately return the same
// bytes are not flagged, because each poll is a transmission that clears the history.
#pragma once

#include <cstddef>
#include <cstdint>

#include "rnet_codec.h"

namespace rnet {

class DuplicateDetector {
 public:
  static constexpr size_t kHistory = 4;

  // Call whenever we transmit a frame (other than an ACK).
  void on_transmit() { count_ = 0; }

  // Returns true if `wire` matches a frame received since the last on_transmit(); `age_ms` is the
  // time since that earlier copy. The frame is then remembered (oldest entry evicted when full).
  bool on_receive(const uint8_t* wire, size_t len, uint32_t now_ms, uint32_t& age_ms);

 private:
  struct Entry {
    uint8_t bytes[kMaxWireBytes];
    size_t len;
    uint32_t at_ms;
  };
  Entry entries_[kHistory];
  size_t count_ = 0;
  size_t next_ = 0;
};

}  // namespace rnet
