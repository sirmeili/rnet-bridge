// USB serial front end for the engine (SPEC §9 M3): prints zone state when it changes, link
// changes and periodic stats, and accepts typed commands that go through the engine's queue. A
// stand-in for the network API until M4. Type `help` for the command list.
#pragma once

#include <Arduino.h>

#include "rnet_engine.h"
#include "zone_model.h"

class UsbCli {
 public:
  // Takes the model's change flags (ZoneModel::take_changed), so nothing else may consume them.
  UsbCli(Print& out, Stream& in, rnet::Engine& engine, rnet::ZoneModel& model);

  void begin();
  // Call every loop(), after Engine::tick(). Never blocks.
  void poll(uint32_t now_ms);

 private:
  static constexpr size_t kLineMax = 80;
  static constexpr uint32_t kStatsIntervalMs = 30000;

  void read_input();
  void execute(char* line);
  void submit(rnet::CommandType type, uint8_t zone, int16_t value,
              rnet::ZoneParam param = rnet::ZoneParam::kBass);
  void print_results();
  void print_zone(uint8_t zone);
  void print_link();
  void print_stats();
  void print_help();

  Print& out_;
  Stream& in_;
  rnet::Engine& engine_;
  rnet::ZoneModel& model_;

  char line_[kLineMax];
  size_t line_len_ = 0;
  uint32_t next_id_ = 1;
  uint32_t now_ms_ = 0;
  uint32_t last_stats_ms_ = 0;
};
