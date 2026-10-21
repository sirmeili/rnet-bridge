#include "rnet_codec.h"

namespace rnet {

uint8_t checksum(const uint8_t* wire, size_t len) {
  uint32_t sum = static_cast<uint32_t>(len);
  for (size_t i = 0; i < len; ++i) {
    sum += wire[i];
  }
  return static_cast<uint8_t>(sum & 0x7F);
}

FrameBuilder::FrameBuilder(const DeviceId& target, const DeviceId& source, uint8_t type) {
  raw(kStartOfMessage);
  put(target.controller).put(target.zone).put(target.keypad);
  put(source.controller).put(source.zone).put(source.keypad);
  put(type);
}

void FrameBuilder::raw(uint8_t b) {
  if (!ok_) {
    return;
  }
  // Reserve two bytes for checksum and F7.
  if (len_ + 2 >= kMaxWireBytes) {
    ok_ = false;
    return;
  }
  buf_[len_++] = b;
}

FrameBuilder& FrameBuilder::put(uint8_t b) {
  if (b > 0x7F) {
    raw(kInvert);
    raw(static_cast<uint8_t>(~b));
  } else {
    raw(b);
  }
  return *this;
}

FrameBuilder& FrameBuilder::put(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    put(data[i]);
  }
  return *this;
}

FrameBuilder& FrameBuilder::put_u16(uint16_t v) {
  put(static_cast<uint8_t>(v & 0xFF));
  return put(static_cast<uint8_t>(v >> 8));
}

bool FrameBuilder::finish(WireFrame& out) {
  if (!ok_) {
    out.len = 0;
    return false;
  }
  const uint8_t cs = checksum(buf_, len_);
  for (size_t i = 0; i < len_; ++i) {
    out.bytes[i] = buf_[i];
  }
  out.bytes[len_] = cs;
  out.bytes[len_ + 1] = kEndOfMessage;
  out.len = len_ + 2;
  return true;
}

bool encode(const Frame& frame, WireFrame& out) {
  if (frame.body_len > kMaxBodyBytes) {
    out.len = 0;
    return false;
  }
  FrameBuilder b(frame.target, frame.source, frame.type);
  b.put(frame.body, frame.body_len);
  return b.finish(out);
}

void FrameParser::reset() {
  state_ = State::kIdle;
  raw_len_ = 0;
}

ParseResult FrameParser::feed(uint8_t b) {
  if (b == kStartOfMessage) {
    const bool was_partial = state_ == State::kInFrame;
    state_ = State::kInFrame;
    raw_[0] = b;
    raw_len_ = 1;
    return was_partial ? ParseResult::kResync : ParseResult::kNone;
  }
  switch (state_) {
    case State::kIdle:
    case State::kDiscarding:
      return ParseResult::kNone;
    case State::kInFrame:
      break;
  }
  if (b == kEndOfMessage) {
    state_ = State::kIdle;
    return complete();
  }
  if (raw_len_ >= kMaxWireBytes - 1) {  // keep room so F0..cs always fits
    state_ = State::kDiscarding;
    raw_len_ = 0;
    return ParseResult::kOverflow;
  }
  raw_[raw_len_++] = b;
  return ParseResult::kNone;
}

ParseResult FrameParser::complete() {
  const size_t n = raw_len_;
  raw_len_ = 0;
  // F0 + escaped content + checksum. Content must hold at least the 7-byte header.
  if (n < 1 + kHeaderBytes + 1) {
    return ParseResult::kMalformed;
  }
  const size_t cs_index = n - 1;
  if (raw_[cs_index] > 0x7F || checksum(raw_, cs_index) != raw_[cs_index]) {
    return ParseResult::kChecksumError;
  }

  uint8_t decoded[kMaxWireBytes];
  size_t dlen = 0;
  for (size_t i = 1; i < cs_index; ++i) {
    uint8_t v = raw_[i];
    if (v == kInvert) {
      if (++i >= cs_index) {
        return ParseResult::kMalformed;  // F1 with nothing after it
      }
      v = static_cast<uint8_t>(~raw_[i]);
    } else if (v > 0x7F) {
      return ParseResult::kMalformed;
    }
    decoded[dlen++] = v;
  }
  if (dlen < kHeaderBytes) {
    return ParseResult::kMalformed;
  }

  frame_.target = {decoded[0], decoded[1], decoded[2]};
  frame_.source = {decoded[3], decoded[4], decoded[5]};
  frame_.type = decoded[6];
  frame_.body_len = dlen - kHeaderBytes;
  for (size_t i = 0; i < frame_.body_len; ++i) {
    frame_.body[i] = decoded[kHeaderBytes + i];
  }
  return ParseResult::kFrame;
}

}  // namespace rnet
