// Build-time configuration: pins, serial settings, and protocol constants that still need hardware
// verification. Everything here is a single named constant so it is easy to change after bring-up.
#pragma once

#include <cstdint>

#include "rnet_messages.h"

namespace config {

constexpr const char* kFirmwareVersion = "0.0.1";

// UART2 on the ESP32-POE-ISO's UEXT header, to the MOD-RS232 (SPEC §2.2, §2.4).
// TODO(SPEC §11.11): unverified. Taken from Olimex demo code, not the schematic. Confirm in M0.
// GPIO36 is input-only, so RX/TX can't be swapped here: use the MOD-RS232's swap jumpers.
// GPIO17 is the Ethernet RMII clock on this board and must not be used.
constexpr int kRnetRxPin = 36;
constexpr int kRnetTxPin = 4;
constexpr uint32_t kRnetBaud = 19200;  // 8N1, no flow control

constexpr uint32_t kUsbSerialBaud = 115200;

// TODO(SPEC §11.1, §11.7): unverified. Hypothesis is a data handshake of type 0x02
// (`F0 00 00 7F 00 zz 70 02 02 cs F7`, SPEC §3.5). Determine on hardware in M2: if the same reply
// arrives again ~2.5 s later, the ACK was not accepted. M2 also checks whether keypad 0x70 needs an
// ACK at all.
constexpr uint8_t kDataHandshakeType = 0x02;

// TODO(SPEC §11.2): unverified. Whether the CAA6.6 needs/accepts the Device Restart event before
// zone Get/Set works. Determine in M2; switch to kNone (or a different event) if it is ignored.
constexpr rnet::SessionStartEvent kSessionStartEvent = rnet::SessionStartEvent::kDeviceRestart;

}  // namespace config
