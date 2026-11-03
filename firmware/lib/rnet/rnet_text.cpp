#include "rnet_text.h"

#include <cstdarg>
#include <cstdio>

#include "rnet_messages.h"

namespace rnet {

namespace {

// Bounded, always NUL-terminated string builder over a caller buffer.
class Writer {
 public:
  Writer(char* out, size_t cap) : out_(out), cap_(cap) {
    if (cap_ > 0) {
      out_[0] = '\0';
    }
  }

  void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (cap_ == 0 || len_ + 1 >= cap_) {
      return;
    }
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(out_ + len_, cap_ - len_, fmt, args);
    va_end(args);
    if (n > 0) {
      len_ += static_cast<size_t>(n);
      if (len_ >= cap_) {
        len_ = cap_ - 1;
      }
    }
  }

  void hex(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
      printf(i == 0 ? "%02X" : " %02X", data[i]);
    }
  }

  void path(const uint8_t* levels, size_t len) {
    if (len == 0) {
      printf("-");
    }
    for (size_t i = 0; i < len; ++i) {
      printf(i == 0 ? "%02X" : ".%02X", levels[i]);
    }
  }

  size_t len() const { return len_; }

 private:
  char* out_;
  size_t cap_;
  size_t len_ = 0;
};

const char* type_name(uint8_t type) {
  switch (static_cast<MsgType>(type)) {
    case MsgType::kSetData:
      return "SetData";
    case MsgType::kRequestData:
      return "Request";
    case MsgType::kHandshake:
      return "Handshake";
    case MsgType::kEvent:
      return "Event";
  }
  return nullptr;
}

const char* event_name(uint16_t id) {
  switch (id) {
    case event_id::kVolumeUp:
      return "VolumeUp";
    case event_id::kVolumeDown:
      return "VolumeDown";
    case event_id::kRemoteKey:
      return "RemoteKey";
    case event_id::kSelectSource:
      return "SelectSource";
    case event_id::kZoneOnOff:
      return "ZoneOnOff";
    case event_id::kAllOnOff:
      return "AllOnOff";
    case event_id::kSetVolume:
      return "SetVolume";
    case event_id::kDeviceRestart:
      return "DeviceRestart";
    default:
      return "?";
  }
}

const char* param_name(ZoneParam p) {
  switch (p) {
    case ZoneParam::kBass:
      return "bass";
    case ZoneParam::kTreble:
      return "treble";
    case ZoneParam::kLoudness:
      return "loud";
    case ZoneParam::kBalance:
      return "bal";
    case ZoneParam::kTurnOnVolume:
      return "tov";
  }
  return "?";
}

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

void describe_event(const Frame& f, Writer& w) {
  // target path count + levels, source path count + levels, id(2) ts(2) data(2) priority(1)
  const uint8_t* b = f.body;
  const size_t n = f.body_len;
  if (n < 1 || n < 1u + b[0] + 1u) {
    w.printf(" body=[");
    w.hex(b, n);
    w.printf("]");
    return;
  }
  const size_t tlen = b[0];
  const size_t slen = b[1 + tlen];
  const size_t fields = 1 + tlen + 1 + slen;
  w.printf(" path=");
  w.path(b + 1, tlen);
  if (n < fields + 7) {
    w.printf(" body=[");
    w.hex(b + fields, n - fields);
    w.printf("]");
    return;
  }
  const uint8_t* e = b + fields;
  w.printf(" id=0x%04X(%s) ts=0x%04X data=0x%04X prio=%u", u16(e), event_name(u16(e)), u16(e + 2),
           u16(e + 4), e[6]);
}

void describe_set_data(const Frame& f, Writer& w) {
  ZoneInfo info{};
  const ReplyResult zi = parse_all_zone_info(f, info);
  if (zi == ReplyResult::kOk) {
    w.printf(" ZoneInfo z%u %s src=%u vol=%u bass=%d treble=%d loud=%s bal=%d sys=%s shared=%s",
             info.zone, info.power ? "on" : "off", info.source, info.volume, info.bass, info.treble,
             info.loudness ? "on" : "off", info.balance, info.system_on ? "on" : "off",
             info.shared_source ? "yes" : "no");
    return;
  }
  ZoneParamValue param{};
  const ReplyResult zp = parse_zone_param(f, param);
  if (zp == ReplyResult::kOk) {
    w.printf(" ZoneParam z%u %s=%d", param.zone, param_name(param.param), param.value);
    return;
  }
  if (zi == ReplyResult::kInvalid || zp == ReplyResult::kInvalid) {
    w.printf(" (INVALID reply)");
  }
  SetDataView v{};
  if (split_set_data(f, v)) {
    w.printf(" tpath=");
    w.path(v.target_path, v.target_path_len);
    w.printf(" spath=");
    w.path(v.source_path, v.source_path_len);
    w.printf(" data=[");
    w.hex(v.data, v.data_len);
    w.printf("]");
    return;
  }
  w.printf(" body=[");
  w.hex(f.body, f.body_len);
  w.printf("]");
}

}  // namespace

size_t format_hex(const uint8_t* data, size_t len, char* out, size_t cap) {
  if (cap == 0) {
    return 0;
  }
  // n bytes need 3n - 1 characters. If they don't all fit, print as many as fit before "...".
  size_t count = len;
  bool truncated = false;
  if (len > 0 && 3 * len - 1 > cap - 1) {
    truncated = true;
    count = cap >= 4 ? (cap - 4 + 1) / 3 : 0;
  }
  static const char kDigits[] = "0123456789ABCDEF";
  size_t pos = 0;
  for (size_t i = 0; i < count; ++i) {
    if (i != 0) {
      out[pos++] = ' ';
    }
    out[pos++] = kDigits[data[i] >> 4];
    out[pos++] = kDigits[data[i] & 0x0F];
  }
  if (truncated) {
    for (int i = 0; i < 3 && pos + 1 < cap; ++i) {
      out[pos++] = '.';
    }
  }
  out[pos] = '\0';
  return pos;
}

bool parse_hex(const char* text, uint8_t* out, size_t cap, size_t& len) {
  len = 0;
  int high = -1;  // pending high nibble within a token
  for (const char* p = text;; ++p) {
    const char c = *p;
    const bool separator = c == '\0' || c == ' ' || c == ',' || c == '\t';
    if (separator) {
      if (high >= 0) {
        return false;  // odd number of digits in a token
      }
      if (c == '\0') {
        return true;
      }
      continue;
    }
    if (c == '0' && (p[1] == 'x' || p[1] == 'X') && high < 0) {
      ++p;  // skip "0x" prefix
      continue;
    }
    int nibble;
    if (c >= '0' && c <= '9') {
      nibble = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      nibble = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      nibble = c - 'A' + 10;
    } else {
      return false;
    }
    if (high < 0) {
      high = nibble;
    } else {
      if (len >= cap) {
        return false;
      }
      out[len++] = static_cast<uint8_t>((high << 4) | nibble);
      high = -1;
    }
  }
}

void describe(const Frame& frame, char* out, size_t cap) {
  Writer w(out, cap);
  const char* name = type_name(frame.type);
  if (name != nullptr) {
    w.printf("%s", name);
  } else {
    w.printf("Type0x%02X", frame.type);
  }
  w.printf(" %02X.%02X.%02X<-%02X.%02X.%02X", frame.target.controller, frame.target.zone,
           frame.target.keypad, frame.source.controller, frame.source.zone, frame.source.keypad);
  switch (static_cast<MsgType>(frame.type)) {
    case MsgType::kEvent:
      describe_event(frame, w);
      return;
    case MsgType::kSetData:
      describe_set_data(frame, w);
      return;
    case MsgType::kHandshake:
      if (frame.body_len >= 1) {
        w.printf(" type=0x%02X", frame.body[0]);
        return;
      }
      break;
    case MsgType::kRequestData:
      break;
  }
  w.printf(" body=[");
  w.hex(frame.body, frame.body_len);
  w.printf("]");
}

}  // namespace rnet
