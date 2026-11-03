// Adapter between HardwareSerial and the engine's byte I/O interface (SPEC §4.2).
#pragma once

#include <Arduino.h>

#include "rnet_engine.h"

class SerialPort : public rnet::ByteIo {
 public:
  explicit SerialPort(HardwareSerial& serial) : serial_(serial) {}

  int read() override { return serial_.available() > 0 ? serial_.read() : -1; }
  // Frames are at most 64 bytes and fit the UART FIFO, so this doesn't block.
  void write(const uint8_t* bytes, size_t len) override { serial_.write(bytes, len); }

 private:
  HardwareSerial& serial_;
};
