// Setup/loop wiring only (SPEC §4.2). M2: a USB serial console for first contact with the
// controller. The engine (M3) and network (M4) come later.
#include <Arduino.h>

#include "config.h"
#include "console.h"

namespace {

Console g_console(Serial, Serial, Serial1);

}  // namespace

void setup() {
  Serial.begin(config::kUsbSerialBaud);
  Serial1.begin(config::kRnetBaud, SERIAL_8N1, config::kRnetRxPin, config::kRnetTxPin);
  g_console.begin();
}

void loop() { g_console.poll(millis()); }
