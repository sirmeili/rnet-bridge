#include "usb_cli.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.h"

namespace {

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

const char* error_name(rnet::CommandError e) {
  switch (e) {
    case rnet::CommandError::kNone:
      return "ok";
    case rnet::CommandError::kBusy:
      return "busy";
    case rnet::CommandError::kInvalid:
      return "invalid";
    case rnet::CommandError::kLinkDown:
      return "link_down";
  }
  return "?";
}

const char* on_off(bool b) { return b ? "on" : "off"; }

}  // namespace

UsbCli::UsbCli(Print& out, Stream& in, rnet::Engine& engine, rnet::ZoneModel& model)
    : out_(out), in_(in), engine_(engine), model_(model) {}

void UsbCli::begin() {
  out_.printf("\r\nRNET Bridge %s: engine (M3). Type 'help'.\r\n", config::kFirmwareVersion);
}

void UsbCli::poll(uint32_t now_ms) {
  now_ms_ = now_ms;
  read_input();
  print_results();
  if (engine_.take_link_changed()) {
    print_link();
  }
  const uint8_t changed = model_.take_changed();
  for (uint8_t z = 1; z <= rnet::kNumZones; ++z) {
    if (changed & (1u << (z - 1))) {
      print_zone(z);
    }
  }
  if (now_ms - last_stats_ms_ >= kStatsIntervalMs) {
    last_stats_ms_ = now_ms;
    print_stats();
  }
}

void UsbCli::read_input() {
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

void UsbCli::execute(char* line) {
  constexpr size_t kMaxArgs = 4;
  char* argv[kMaxArgs] = {};
  size_t argc = 0;
  for (char* tok = std::strtok(line, " \t"); tok != nullptr && argc < kMaxArgs;
       tok = std::strtok(nullptr, " \t")) {
    argv[argc++] = tok;
  }
  if (argc == 0) {
    return;
  }
  const char* cmd = argv[0];
  auto is = [cmd](const char* name) { return std::strcmp(cmd, name) == 0; };
  long zone = 0;
  long value = 0;
  // Zone ranges are checked by the engine; parse_int only rejects junk here.
  const bool has_zone = parse_int(argv[1], 0, 99, zone);
  const auto z = static_cast<uint8_t>(zone);

  if (is("help") || is("?")) {
    print_help();
  } else if (is("state")) {
    for (uint8_t i = 1; i <= rnet::kNumZones; ++i) {
      print_zone(i);
    }
    print_link();
  } else if (is("stats")) {
    print_stats();
  } else if ((is("on") || is("off")) && has_zone) {
    submit(rnet::CommandType::kSetPower, z, is("on") ? 1 : 0);
  } else if (is("src") && has_zone && parse_int(argv[2], -999, 999, value)) {
    submit(rnet::CommandType::kSetSource, z, static_cast<int16_t>(value));
  } else if (is("vol") && has_zone && parse_int(argv[2], -999, 999, value)) {
    submit(rnet::CommandType::kSetVolume, z, static_cast<int16_t>(value));
  } else if ((is("up") || is("down")) && has_zone) {
    submit(is("up") ? rnet::CommandType::kVolumeUp : rnet::CommandType::kVolumeDown, z, 0);
  } else if (is("mute") && has_zone && argv[2] != nullptr &&
             (std::strcmp(argv[2], "on") == 0 || std::strcmp(argv[2], "off") == 0)) {
    submit(rnet::CommandType::kSetMute, z, std::strcmp(argv[2], "on") == 0 ? 1 : 0);
  } else if (is("set") && has_zone) {
    rnet::ZoneParam param{};
    if (parse_param(argv[2], param) && parse_int(argv[3], -999, 999, value)) {
      submit(rnet::CommandType::kSetParam, z, static_cast<int16_t>(value), param);
    } else {
      out_.print("usage: set <z> bass|treble|bal|loud|tov <value>\r\n");
    }
  } else if (is("allon") || is("alloff")) {
    submit(is("allon") ? rnet::CommandType::kAllOn : rnet::CommandType::kAllOff, 0, 0);
  } else if (is("refresh")) {
    submit(rnet::CommandType::kRefresh, has_zone ? z : 0, 0);
  } else {
    out_.print("unknown or incomplete command (try 'help')\r\n");
  }
}

void UsbCli::submit(rnet::CommandType type, uint8_t zone, int16_t value, rnet::ZoneParam param) {
  const uint32_t id = next_id_++;
  const rnet::CommandError e = engine_.submit(rnet::Command{id, type, zone, value, param});
  if (e != rnet::CommandError::kNone) {
    out_.printf("[%8lu] cmd %lu: %s\r\n", static_cast<unsigned long>(now_ms_),
                static_cast<unsigned long>(id), error_name(e));
  }
}

void UsbCli::print_results() {
  rnet::CommandResult r{};
  while (engine_.next_result(r)) {
    out_.printf("[%8lu] cmd %lu: %s\r\n", static_cast<unsigned long>(now_ms_),
                static_cast<unsigned long>(r.id), error_name(r.error));
  }
}

void UsbCli::print_zone(uint8_t zone) {
  const rnet::ZoneState& s = model_.zone(zone);
  if (!s.valid) {
    out_.printf("[%8lu] z%u (not polled yet)\r\n", static_cast<unsigned long>(now_ms_), zone);
    return;
  }
  char tov[8] = "?";
  if (s.turn_on_volume_valid) {
    std::snprintf(tov, sizeof(tov), "%u", s.turn_on_volume);
  }
  out_.printf("[%8lu] z%u %s src=%u vol=%u%s bass=%d treble=%d bal=%d loud=%s tov=%s sys=%s "
              "shared=%s\r\n",
              static_cast<unsigned long>(now_ms_), zone, on_off(s.power), s.source, s.volume,
              s.muted ? " MUTED" : "", s.bass, s.treble, s.balance, on_off(s.loudness), tov,
              on_off(s.system_on), s.shared_source ? "yes" : "no");
}

void UsbCli::print_link() {
  out_.printf("[%8lu] link %s\r\n", static_cast<unsigned long>(now_ms_),
              engine_.link_up() ? "UP" : "DOWN");
}

void UsbCli::print_stats() {
  const rnet::EngineStats& s = engine_.stats();
  out_.printf("[%8lu] stats link=%s tx=%lu rx=%lu checksum_errors=%lu dropped=%lu timeouts=%lu "
              "retries=%lu duplicate_replies=%lu handshakes_rx=%lu queue_depth=%u\r\n",
              static_cast<unsigned long>(now_ms_), engine_.link_up() ? "up" : "down",
              static_cast<unsigned long>(s.tx), static_cast<unsigned long>(s.rx),
              static_cast<unsigned long>(s.checksum_errors),
              static_cast<unsigned long>(s.dropped), static_cast<unsigned long>(s.timeouts),
              static_cast<unsigned long>(s.retries),
              static_cast<unsigned long>(s.duplicate_replies),
              static_cast<unsigned long>(s.handshakes_rx),
              static_cast<unsigned>(engine_.queue_depth()));
}

void UsbCli::print_help() {
  out_.print(
      "Zones and sources are 1-based. Zone changes print automatically.\r\n"
      "  state | stats            all zones and link | counters\r\n"
      "  on <z> | off <z>         zone power\r\n"
      "  src <z> <1-6>            select source\r\n"
      "  vol <z> <0-50>           set volume (raw; keypad shows x2)\r\n"
      "  up <z> | down <z>        volume step\r\n"
      "  mute <z> on|off          emulated mute\r\n"
      "  set <z> <param> <value>  bass/treble/bal -10..10, loud 0/1, tov 0-50\r\n"
      "  allon | alloff           all zones (All On does nothing on the CAA6.6)\r\n"
      "  refresh [z]              poll now (all zones if omitted)\r\n");
}
