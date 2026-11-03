#include "console.h"

#include <cstdlib>
#include <cstring>

#include "config.h"
#include "rnet_text.h"

namespace {

constexpr size_t kHexMax = rnet::kMaxWireBytes * 3 + 4;
constexpr size_t kDescribeMax = 220;

// Parses a decimal integer in [lo, hi]. Returns false on junk or out of range.
bool parse_int(const char* s, long lo, long hi, long& value) {
  if (s == nullptr || *s == '\0') {
    return false;
  }
  char* end = nullptr;
  const long v = std::strtol(s, &end, 10);
  if (*end != '\0' || v < lo || v > hi) {
    return false;
  }
  value = v;
  return true;
}

bool parse_zone(const char* s, uint8_t& zone) {
  long v = 0;
  if (!parse_int(s, 1, rnet::kNumZones, v)) {
    return false;
  }
  zone = static_cast<uint8_t>(v);
  return true;
}

bool parse_param(const char* s, rnet::ZoneParam& param) {
  if (s == nullptr) {
    return false;
  }
  struct Name {
    const char* name;
    rnet::ZoneParam param;
  };
  static const Name kNames[] = {
      {"bass", rnet::ZoneParam::kBass},       {"treble", rnet::ZoneParam::kTreble},
      {"loud", rnet::ZoneParam::kLoudness},   {"bal", rnet::ZoneParam::kBalance},
      {"tov", rnet::ZoneParam::kTurnOnVolume},
  };
  for (const Name& n : kNames) {
    if (std::strcmp(s, n.name) == 0) {
      param = n.param;
      return true;
    }
  }
  return false;
}

bool parse_on_off(const char* s, bool& on) {
  if (s != nullptr && std::strcmp(s, "on") == 0) {
    on = true;
    return true;
  }
  if (s != nullptr && std::strcmp(s, "off") == 0) {
    on = false;
    return true;
  }
  return false;
}

bool parse_byte_hex(const char* s, uint8_t& b) {
  uint8_t buf[1];
  size_t len = 0;
  if (s == nullptr || !rnet::parse_hex(s, buf, 1, len) || len != 1) {
    return false;
  }
  b = buf[0];
  return true;
}

}  // namespace

Console::Console(Print& out, Stream& in, HardwareSerial& rnet)
    : out_(out), in_(in), rnet_(rnet), ack_type_(config::kDataHandshakeType) {}

void Console::begin() {
  out_.printf("\r\nRNET Bridge %s: M2 serial console\r\n", config::kFirmwareVersion);
  out_.printf("RNET on UART1 RX=GPIO%d TX=GPIO%d, %lu 8N1. Type 'help'.\r\n", config::kRnetRxPin,
              config::kRnetTxPin, static_cast<unsigned long>(config::kRnetBaud));
  print_prompt();
}

void Console::poll(uint32_t now_ms) {
  now_ms_ = now_ms;
  read_usb();
  read_rnet(now_ms);
  check_timeouts(now_ms);
}

void Console::print_prompt() { out_.print("> "); }

void Console::read_usb() {
  while (in_.available() > 0) {
    const int c = in_.read();
    if (c == '\r' || c == '\n') {
      if (line_len_ == 0) {
        continue;
      }
      out_.print("\r\n");
      line_[line_len_] = '\0';
      execute(line_);
      line_len_ = 0;
      print_prompt();
    } else if (c == 0x08 || c == 0x7F) {
      if (line_len_ > 0) {
        --line_len_;
        out_.print("\b \b");
      }
    } else if (c >= 0x20 && c < 0x7F && line_len_ + 1 < kLineMax) {
      line_[line_len_++] = static_cast<char>(c);
      out_.write(static_cast<uint8_t>(c));  // echo: the monitor has no local echo
    }
  }
}

void Console::read_rnet(uint32_t now_ms) {
  while (rnet_.available() > 0) {
    const auto b = static_cast<uint8_t>(rnet_.read());
    last_rx_byte_ms_ = now_ms;
    if (loopback_active_) {
      if (loopback_len_ < kLoopbackBytes) {
        loopback_got_[loopback_len_++] = b;
      }
      if (loopback_len_ == kLoopbackBytes) {
        finish_loopback();
      }
      continue;
    }
    switch (parser_.feed(b)) {
      case rnet::ParseResult::kFrame:
        on_frame(now_ms);
        break;
      case rnet::ParseResult::kChecksumError:
        ++stats_.checksum_errors;
        on_rx_error("checksum error");
        break;
      case rnet::ParseResult::kMalformed:
        ++stats_.dropped;
        on_rx_error("malformed frame");
        break;
      case rnet::ParseResult::kResync:
        ++stats_.dropped;
        out_.printf("\r\n[%8lu] RX partial frame dropped (new F0 arrived)\r\n",
                    static_cast<unsigned long>(now_ms));
        break;
      case rnet::ParseResult::kOverflow:
        ++stats_.dropped;
        out_.printf("\r\n[%8lu] RX frame too long, discarded\r\n",
                    static_cast<unsigned long>(now_ms));
        break;
      case rnet::ParseResult::kNone:
        break;
    }
  }
}

void Console::check_timeouts(uint32_t now_ms) {
  if (parser_.in_frame() && now_ms - last_rx_byte_ms_ > config::kRnetInterByteTimeoutMs) {
    parser_.reset();
    ++stats_.timeouts;
    out_.printf("\r\n[%8lu] RX partial frame dropped (inter-byte timeout)\r\n",
                static_cast<unsigned long>(now_ms));
  }
  if (loopback_active_ && static_cast<int32_t>(now_ms - loopback_deadline_ms_) >= 0) {
    finish_loopback();
  }
}

void Console::on_frame(uint32_t now_ms) {
  ++stats_.rx_frames;
  const rnet::Frame& f = parser_.frame();
  char hex[kHexMax];
  char text[kDescribeMax];
  rnet::format_hex(parser_.last_wire(), parser_.last_wire_len(), hex, sizeof(hex));
  rnet::describe(f, text, sizeof(text));
  out_.printf("\r\n[%8lu] RX %s\r\n           %s\r\n", static_cast<unsigned long>(now_ms), hex,
              text);

  uint32_t age_ms = 0;
  if (duplicates_.on_receive(parser_.last_wire(), parser_.last_wire_len(), now_ms, age_ms)) {
    ++stats_.duplicates;
    out_.printf("           !! DUPLICATE of a frame received %lu ms ago: retransmit, ACK not "
                "accepted? (SPEC 3.5)\r\n",
                static_cast<unsigned long>(age_ms));
  }

  uint8_t hs_type = 0;
  if (rnet::parse_handshake(f, hs_type) == rnet::ReplyResult::kOk) {
    ++stats_.handshakes_rx;
  }

  // Data replies addressed to us (keypad 0x70) get an ACK when auto-ack is on.
  const bool for_us = f.target.keypad == rnet::kBridgeKeypadId && f.target.zone < rnet::kNumZones;
  if (auto_ack_ && for_us && f.type == static_cast<uint8_t>(rnet::MsgType::kSetData)) {
    rnet::WireFrame ack{};
    if (rnet::build_data_handshake(static_cast<uint8_t>(f.target.zone + 1), ack_type_, ack)) {
      send(ack, "auto-ack", true);
    }
  }
}

void Console::on_rx_error(const char* what) {
  char hex[kHexMax];
  rnet::format_hex(parser_.last_wire(), parser_.last_wire_len(), hex, sizeof(hex));
  out_.printf("\r\n[%8lu] RX %s: %s\r\n", static_cast<unsigned long>(now_ms_), what, hex);
}

void Console::send(const rnet::WireFrame& frame, const char* label, bool is_ack) {
  rnet_.write(frame.bytes, frame.len);  // <= 64 bytes: fits the UART FIFO, no blocking
  ++stats_.tx_frames;
  if (is_ack) {
    ++stats_.acks_tx;
  } else {
    duplicates_.on_transmit();
  }
  char hex[kHexMax];
  rnet::format_hex(frame.bytes, frame.len, hex, sizeof(hex));
  out_.printf("[%8lu] TX %s (%s)\r\n", static_cast<unsigned long>(now_ms_), hex, label);
}

void Console::send_or_report(bool built, const rnet::WireFrame& frame, const char* label) {
  if (built) {
    send(frame, label, false);
  } else {
    out_.print("error: invalid arguments (see 'help')\r\n");
  }
}

void Console::start_loopback(uint32_t now_ms) {
  for (size_t i = 0; i < kLoopbackBytes; ++i) {
    loopback_sent_[i] = static_cast<uint8_t>(i * 37 + 11);  // covers high and low byte values
  }
  loopback_len_ = 0;
  loopback_active_ = true;
  loopback_deadline_ms_ = now_ms + kLoopbackTimeoutMs;
  parser_.reset();
  rnet_.write(loopback_sent_, kLoopbackBytes);
  out_.printf("loopback: sent %u bytes, waiting for echo...\r\n",
              static_cast<unsigned>(kLoopbackBytes));
}

void Console::finish_loopback() {
  loopback_active_ = false;
  size_t mismatches = 0;
  size_t first_bad = kLoopbackBytes;
  for (size_t i = 0; i < loopback_len_; ++i) {
    if (loopback_got_[i] != loopback_sent_[i]) {
      ++mismatches;
      if (first_bad == kLoopbackBytes) {
        first_bad = i;
      }
    }
  }
  if (loopback_len_ == kLoopbackBytes && mismatches == 0) {
    out_.print("loopback: PASS (64/64 bytes echoed intact)\r\n");
  } else if (loopback_len_ == 0) {
    out_.print("loopback: FAIL, nothing came back. Check that DB9 pins 2-3 are shorted on the "
               "MOD-RS232, the UEXT cable is seated, and jumpers 3_TX/4_RX are closed.\r\n");
  } else {
    out_.printf("loopback: FAIL, %u/%u bytes back, %u mismatched", static_cast<unsigned>(loopback_len_),
                static_cast<unsigned>(kLoopbackBytes), static_cast<unsigned>(mismatches));
    if (first_bad < kLoopbackBytes) {
      out_.printf(" (first at #%u: sent %02X got %02X)", static_cast<unsigned>(first_bad),
                  loopback_sent_[first_bad], loopback_got_[first_bad]);
    }
    out_.print("\r\n");
  }
  print_prompt();
}

void Console::print_help() {
  out_.print(
      "Zones and sources are 1-based. Every RX frame is printed and decoded.\r\n"
      "  loopback                 echo test; ONLY with MOD-RS232 DB9 pins 2-3 shorted\r\n"
      "  on <z> | off <z>         zone power\r\n"
      "  allon | alloff           all zones power (All On may be unsupported, SPEC 11.9)\r\n"
      "  src <z> <s>              select source 1-6\r\n"
      "  vol <z> <0-50>           set volume (raw 0-50)\r\n"
      "  volup <z> | voldn <z>    keypad volume up/down events\r\n"
      "  info <z>                 request all-zone-info\r\n"
      "  get <z> <param>          request zone param: bass treble loud bal tov\r\n"
      "  set <z> <param> <value>  set zone param (bass/treble/bal -10..10, loud 0/1, tov 0-50)\r\n"
      "  restart                  send the session-start Device Restart event (SPEC 3.6)\r\n"
      "  ack <z> [type]           send a data handshake now (type hex, default = acktype)\r\n"
      "  autoack on|off           ACK data replies automatically (default off)\r\n"
      "  acktype <hex>            handshake type byte for ACKs (default from config.h)\r\n"
      "  pwrsrc 00|zz             Zone On/Off source zone byte (SPEC 11.8, default 00)\r\n"
      "  raw <hex...>             send bytes exactly as typed\r\n"
      "  rawfix <hex...>          send F0..last body byte, appending checksum + F7\r\n"
      "  stats                    counters and current settings\r\n");
}

void Console::print_stats() {
  out_.printf("tx=%lu rx=%lu checksum_errors=%lu dropped=%lu timeouts=%lu duplicates=%lu "
              "handshakes_rx=%lu acks_tx=%lu\r\n",
              static_cast<unsigned long>(stats_.tx_frames),
              static_cast<unsigned long>(stats_.rx_frames),
              static_cast<unsigned long>(stats_.checksum_errors),
              static_cast<unsigned long>(stats_.dropped),
              static_cast<unsigned long>(stats_.timeouts),
              static_cast<unsigned long>(stats_.duplicates),
              static_cast<unsigned long>(stats_.handshakes_rx),
              static_cast<unsigned long>(stats_.acks_tx));
  out_.printf("autoack=%s acktype=0x%02X pwrsrc=%s\r\n", auto_ack_ ? "on" : "off", ack_type_,
              power_source_zone_ == rnet::PowerSourceZone::kZone ? "zz" : "00");
}

void Console::execute(char* line) {
  constexpr size_t kMaxArgs = rnet::kMaxWireBytes + 2;
  char* argv[kMaxArgs];
  size_t argc = 0;
  for (char* tok = std::strtok(line, " \t"); tok != nullptr && argc < kMaxArgs;
       tok = std::strtok(nullptr, " \t")) {
    argv[argc++] = tok;
  }
  if (argc == 0) {
    return;
  }
  const char* cmd = argv[0];
  const char* a1 = argc > 1 ? argv[1] : nullptr;
  const char* a2 = argc > 2 ? argv[2] : nullptr;
  const char* a3 = argc > 3 ? argv[3] : nullptr;
  auto is = [cmd](const char* name) { return std::strcmp(cmd, name) == 0; };

  if (loopback_active_) {
    out_.print("busy: loopback in progress\r\n");
    return;
  }

  rnet::WireFrame frame{};
  uint8_t zone = 0;
  long value = 0;

  if (is("help") || is("?")) {
    print_help();
  } else if (is("stats")) {
    print_stats();
  } else if (is("loopback")) {
    start_loopback(now_ms_);
  } else if (is("on") || is("off")) {
    const bool ok = parse_zone(a1, zone);
    send_or_report(ok && rnet::build_zone_power(zone, is("on"), frame, power_source_zone_), frame,
                   cmd);
  } else if (is("allon") || is("alloff")) {
    send_or_report(rnet::build_all_zones_power(is("allon"), frame), frame, cmd);
  } else if (is("src")) {
    const bool ok = parse_zone(a1, zone) && parse_int(a2, 1, rnet::kNumSources, value);
    send_or_report(ok && rnet::build_select_source(zone, static_cast<uint8_t>(value), frame), frame,
                   "select source");
  } else if (is("vol")) {
    const bool ok = parse_zone(a1, zone) && parse_int(a2, 0, rnet::kMaxVolume, value);
    send_or_report(ok && rnet::build_set_volume(zone, static_cast<uint8_t>(value), frame), frame,
                   "set volume");
  } else if (is("volup") || is("voldn")) {
    const uint16_t ev = is("volup") ? rnet::event_id::kVolumeUp : rnet::event_id::kVolumeDown;
    send_or_report(parse_zone(a1, zone) && rnet::build_keypad_event(zone, ev, frame), frame, cmd);
  } else if (is("info")) {
    send_or_report(parse_zone(a1, zone) && rnet::build_get_all_zone_info(zone, frame), frame,
                   "get all zone info");
  } else if (is("get")) {
    rnet::ZoneParam param{};
    const bool ok = parse_zone(a1, zone) && parse_param(a2, param);
    send_or_report(ok && rnet::build_get_zone_param(zone, param, frame), frame, "get zone param");
  } else if (is("set")) {
    rnet::ZoneParam param{};
    const bool ok = parse_zone(a1, zone) && parse_param(a2, param) && parse_int(a3, -128, 127, value);
    send_or_report(
        ok && rnet::build_set_zone_param(zone, param, static_cast<int8_t>(value), frame), frame,
        "set zone param");
  } else if (is("restart")) {
    send_or_report(rnet::build_session_start(rnet::SessionStartEvent::kDeviceRestart, frame), frame,
                   "device restart");
  } else if (is("ack")) {
    uint8_t type = ack_type_;
    const bool ok = parse_zone(a1, zone) && (a2 == nullptr || parse_byte_hex(a2, type));
    if (ok && rnet::build_data_handshake(zone, type, frame)) {
      send(frame, "manual ack", true);
    } else {
      out_.print("error: usage: ack <zone> [type hex]\r\n");
    }
  } else if (is("autoack")) {
    bool on = false;
    if (parse_on_off(a1, on)) {
      auto_ack_ = on;
    }
    print_stats();
  } else if (is("acktype")) {
    uint8_t type = 0;
    if (parse_byte_hex(a1, type)) {
      ack_type_ = type;
    } else {
      out_.print("error: usage: acktype <hex byte>\r\n");
    }
    print_stats();
  } else if (is("pwrsrc")) {
    if (a1 != nullptr && std::strcmp(a1, "00") == 0) {
      power_source_zone_ = rnet::PowerSourceZone::kZero;
    } else if (a1 != nullptr && std::strcmp(a1, "zz") == 0) {
      power_source_zone_ = rnet::PowerSourceZone::kZone;
    } else {
      out_.print("error: usage: pwrsrc 00|zz\r\n");
    }
    print_stats();
  } else if (is("raw") || is("rawfix")) {
    // Re-join the remaining tokens and parse them as hex.
    uint8_t bytes[rnet::kMaxWireBytes];
    size_t len = 0;
    bool ok = argc > 1;
    for (size_t i = 1; ok && i < argc; ++i) {
      size_t n = 0;
      ok = rnet::parse_hex(argv[i], bytes + len, sizeof(bytes) - len, n);
      len += n;
    }
    if (ok && is("rawfix")) {
      ok = len + 2 <= sizeof(bytes);
      if (ok) {
        bytes[len] = rnet::checksum(bytes, len);
        bytes[len + 1] = rnet::kEndOfMessage;
        len += 2;
      }
    }
    if (!ok || len == 0) {
      out_.print("error: bad hex (max 64 bytes)\r\n");
      return;
    }
    for (size_t i = 0; i < len; ++i) {
      frame.bytes[i] = bytes[i];
    }
    frame.len = len;
    send(frame, cmd, false);
  } else {
    out_.printf("unknown command '%s' (try 'help')\r\n", cmd);
  }
}
