// Build-time configuration: pins, serial settings, and protocol constants that still need hardware
// verification. Everything here is a single named constant so it is easy to change after bring-up.
#pragma once

#include <cstdint>

#include "rnet_messages.h"

namespace config {

constexpr const char* kFirmwareVersion = "0.2.0";

// UART1 to the Olimex MOD-RS232 via the UEXT connector (SPEC §2.2, §2.4): UEXT pin 3 = GPIO4 (TX),
// UEXT pin 4 = GPIO36 (RX, input-only). Verified against the ESP32-POE-ISO Rev O schematic.
// GPIO36 can't transmit, so RX/TX can't be swapped here. GPIO17 is the Ethernet clock; don't use it.
constexpr int kRnetRxPin = 36;
constexpr int kRnetTxPin = 4;
constexpr uint32_t kRnetBaud = 19200;  // 8N1, no flow control

// A partial RX frame is dropped if no byte arrives for this long. One byte at 19200 baud takes
// ~0.5 ms, and frames are sent back-to-back.
constexpr uint32_t kRnetInterByteTimeoutMs = 50;

constexpr uint32_t kUsbSerialBaud = 115200;

// SPEC §3.5, verified 2026-11-02 (docs/rnet-notes.md M2.4): with keypad 0x70 the CAA6.6 never
// retransmits an unacknowledged reply, so the bridge sends no data ACKs. kDataHandshakeType is the
// type to use if they are ever re-enabled; keypads on the bus ACK the controller with 0x02.
constexpr bool kSendDataAck = false;
constexpr uint8_t kDataHandshakeType = 0x02;

// SPEC §11.2, verified 2026-11-02 (docs/rnet-notes.md M2.2): the CAA6.6 answers zone Get/Set with no
// session-start event, including as the first frame after a controller power-cycle. Send nothing.
constexpr rnet::SessionStartEvent kSessionStartEvent = rnet::SessionStartEvent::kNone;

}  // namespace config
