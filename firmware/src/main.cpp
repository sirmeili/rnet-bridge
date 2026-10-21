// Setup/loop wiring only (SPEC §4.2). For M1 this just brings up UART2 and runs received bytes
// through the RNET parser, reporting counters. The serial console (M2) and engine (M3) come later.
#include <Arduino.h>

#include "config.h"
#include "rnet_codec.h"

namespace {

rnet::FrameParser g_parser;
uint32_t g_frames = 0;
uint32_t g_checksum_errors = 0;
uint32_t g_dropped = 0;
uint32_t g_last_report_ms = 0;
constexpr uint32_t kReportIntervalMs = 5000;

void drain_rnet_rx() {
  while (Serial2.available() > 0) {
    switch (g_parser.feed(static_cast<uint8_t>(Serial2.read()))) {
      case rnet::ParseResult::kFrame:
        ++g_frames;
        break;
      case rnet::ParseResult::kChecksumError:
        ++g_checksum_errors;
        break;
      case rnet::ParseResult::kResync:
      case rnet::ParseResult::kOverflow:
      case rnet::ParseResult::kMalformed:
        ++g_dropped;
        break;
      case rnet::ParseResult::kNone:
        break;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(config::kUsbSerialBaud);
  Serial2.begin(config::kRnetBaud, SERIAL_8N1, config::kRnetRxPin, config::kRnetTxPin);
  Serial.printf("RNET Bridge %s (M1: codec only, RX monitor)\n", config::kFirmwareVersion);
}

void loop() {
  drain_rnet_rx();
  const uint32_t now = millis();
  if (now - g_last_report_ms >= kReportIntervalMs) {
    g_last_report_ms = now;
    Serial.printf("rnet rx: frames=%lu checksum_errors=%lu dropped=%lu\n",
                  static_cast<unsigned long>(g_frames),
                  static_cast<unsigned long>(g_checksum_errors),
                  static_cast<unsigned long>(g_dropped));
  }
}
