// Setup/loop for the `esp32-console` build env: the M2 bring-up console (SPEC §9 M2). Kept for
// bring-up on other controller models and for checking protocol details that don't match later.
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
