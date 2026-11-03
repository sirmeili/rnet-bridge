// Setup/loop wiring only (SPEC §4.2). M3: the RNET engine, with a USB serial front end in place of
// the network API (M4). The M2 bring-up console is the separate `esp32-console` env.
#include <Arduino.h>

#include "config.h"
#include "platform/serial_port.h"
#include "rnet_engine.h"
#include "usb_cli.h"
#include "zone_model.h"

namespace {

rnet::EngineConfig engine_config() {
  rnet::EngineConfig c;
  c.inter_byte_timeout_ms = config::kRnetInterByteTimeoutMs;
  c.send_data_ack = config::kSendDataAck;
  c.data_handshake_type = config::kDataHandshakeType;
  c.session_start = config::kSessionStartEvent;
  return c;
}

SerialPort g_port(Serial1);
rnet::ZoneModel g_model;
rnet::Engine g_engine(g_port, g_model, engine_config());
UsbCli g_cli(Serial, Serial, g_engine, g_model);

}  // namespace

void setup() {
  Serial.begin(config::kUsbSerialBaud);
  Serial1.begin(config::kRnetBaud, SERIAL_8N1, config::kRnetRxPin, config::kRnetTxPin);
  g_cli.begin();
}

void loop() {
  const uint32_t now = millis();
  g_engine.tick(now);
  g_cli.poll(now);
}
