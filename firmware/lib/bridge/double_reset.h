// Double-reset detection (SPEC §4.4): pressing reset twice within 3 s forces the setup portal,
// because the board has no spare button outside the case. A flag is stored in NVS at boot and
// cleared 3 s later; finding it already set at boot means the previous boot was reset within 3 s.
// The storage is the caller's; this class only decides. Pure C++17.
#pragma once

#include <cstdint>

namespace bridge {

class DoubleResetDetector {
 public:
  static constexpr uint32_t kWindowMs = 3000;

  // Call once at boot with the stored flag. Returns true for a double reset. The caller then stores
  // the flag as set.
  bool begin(bool flag_was_set, uint32_t now_ms) {
    start_ms_ = now_ms;
    pending_clear_ = true;
    return flag_was_set;
  }

  // Returns true exactly once, when the window has passed: the caller then clears the flag.
  bool should_clear(uint32_t now_ms) {
    if (pending_clear_ && now_ms - start_ms_ >= kWindowMs) {
      pending_clear_ = false;
      return true;
    }
    return false;
  }

 private:
  uint32_t start_ms_ = 0;
  bool pending_clear_ = false;
};

}  // namespace bridge
