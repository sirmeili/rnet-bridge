// RNET wire codec: escaping, checksum, frame building and byte-at-a-time parsing (SPEC §3.1).
// Pure C++17, no Arduino dependencies, no heap allocation.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rnet {

constexpr uint8_t kStartOfMessage = 0xF0;
constexpr uint8_t kEndOfMessage = 0xF7;
constexpr uint8_t kInvert = 0xF1;

enum class MsgType : uint8_t {
  kSetData = 0x00,
  kRequestData = 0x01,
  kHandshake = 0x02,
  kEvent = 0x05,
};

// Largest frame we expect is ~42 wire bytes (display feedback, PDF §10). Leave headroom.
constexpr size_t kMaxWireBytes = 64;
// Decoded header: target (3) + source (3) + type (1).
constexpr size_t kHeaderBytes = 7;
constexpr size_t kMaxBodyBytes = kMaxWireBytes - 1 - kHeaderBytes - 2;

struct DeviceId {
  uint8_t controller;
  uint8_t zone;
  uint8_t keypad;
};

// A decoded (unescaped) frame. `body` excludes the header, checksum and F7.
struct Frame {
  DeviceId target;
  DeviceId source;
  uint8_t type;
  uint8_t body[kMaxBodyBytes];
  size_t body_len;
};

// Encoded frame bytes, F0 through F7 inclusive, ready to transmit.
struct WireFrame {
  uint8_t bytes[kMaxWireBytes];
  size_t len;
};

// RNET checksum: sum of `len` transmitted bytes (F0 through the last body byte, escapes included),
// plus `len`, masked to 7 bits.
uint8_t checksum(const uint8_t* wire, size_t len);

// Builds an escaped frame into a fixed buffer. Append logical bytes with put(); bytes > 0x7F are
// sent as F1 followed by the inverted byte. A failed put() or finish() leaves the builder in an
// error state, and finish() then returns false.
class FrameBuilder {
 public:
  FrameBuilder(const DeviceId& target, const DeviceId& source, uint8_t type);

  FrameBuilder& put(uint8_t b);
  FrameBuilder& put(const uint8_t* data, size_t len);
  FrameBuilder& put_u16(uint16_t v);  // little endian: lo, hi

  // Appends checksum and F7 and copies the result to `out`.
  bool finish(WireFrame& out);

 private:
  void raw(uint8_t b);

  uint8_t buf_[kMaxWireBytes];
  size_t len_ = 0;
  bool ok_ = true;
};

// Encodes a decoded frame (header + body).
bool encode(const Frame& frame, WireFrame& out);

enum class ParseResult : uint8_t {
  kNone,           // byte consumed, nothing to report
  kFrame,          // a complete, checksum-valid frame is available via frame()
  kChecksumError,  // frame discarded: checksum mismatch
  kResync,         // F0 arrived mid-frame: partial frame discarded, new frame started
  kOverflow,       // frame exceeded kMaxWireBytes: discarded until next F0
  kMalformed,      // frame discarded: too short, stray byte > 0x7F, or dangling F1
};

// Streaming parser. Feed received bytes one at a time. Bytes outside a frame are ignored.
// The frame returned by frame() stays valid until the next call to feed() or reset().
class FrameParser {
 public:
  ParseResult feed(uint8_t b);
  // Drops any partial frame (e.g. on an inter-byte timeout).
  void reset();
  bool in_frame() const { return state_ == State::kInFrame; }
  const Frame& frame() const { return frame_; }
  // Wire bytes (F0 through F7, as received) of the frame that the last kFrame, kChecksumError or
  // kMalformed result refers to. Valid until the next call to feed() or reset().
  const uint8_t* last_wire() const { return raw_; }
  size_t last_wire_len() const { return last_wire_len_; }

 private:
  enum class State : uint8_t { kIdle, kInFrame, kDiscarding };

  ParseResult complete();

  State state_ = State::kIdle;
  uint8_t raw_[kMaxWireBytes];  // F0 through checksum (plus F7 once complete), as received
  size_t raw_len_ = 0;
  size_t last_wire_len_ = 0;
  Frame frame_{};
};

}  // namespace rnet
