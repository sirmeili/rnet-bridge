// Setup/loop wiring only (SPEC §4.2). The RNET engine, the network (Ethernet first, Wi-Fi fallback,
// portal), the HTTP/WebSocket API (docs/protocol.md), mDNS, OTA, and a USB serial front end. The M2
// bring-up console is the separate `esp32-console` env.
#include <Arduino.h>
#include <freertos/semphr.h>

#include <cstdio>

#include "config.h"
#include "double_reset.h"
#include "net/api_server.h"
#include "net/mdns.h"
#include "net/network.h"
#include "net/ota.h"
#include "net/settings.h"
#include "platform/log.h"
#include "platform/serial_port.h"
#include "rnet_engine.h"
#include "usb_cli.h"
#include "zone_model.h"

Log g_log(Serial);

namespace {

constexpr uint32_t kRestartDelayMs = 1000;  // lets the HTTP response or portal page go out first

rnet::EngineConfig engine_config() {
  rnet::EngineConfig c;
  c.inter_byte_timeout_ms = config::kRnetInterByteTimeoutMs;
  c.send_data_ack = config::kSendDataAck;
  c.data_handshake_type = config::kDataHandshakeType;
  c.session_start = config::kSessionStartEvent;
  return c;
}

SemaphoreHandle_t g_lock = nullptr;  // engine + model state, shared with the status page
Settings g_settings;
SerialPort g_port(Serial1);
rnet::ZoneModel g_model;
rnet::Engine g_engine(g_port, g_model, engine_config());
Network g_network(g_settings);
ApiServer* g_api = nullptr;
UsbCli g_cli(Serial, Serial, g_engine, g_model, ApiServer::kCliIdBit);
bridge::DoubleResetDetector g_double_reset;
bool g_restart_pending = false;
uint32_t g_restart_at_ms = 0;

void schedule_restart(uint32_t now_ms) {
  if (!g_restart_pending) {
    g_restart_pending = true;
    g_restart_at_ms = now_ms + kRestartDelayMs;
    LOG_WARN("restarting in %lu ms", static_cast<unsigned long>(kRestartDelayMs));
  }
}

}  // namespace

void setup() {
  Serial.begin(config::kUsbSerialBaud);
  Serial1.begin(config::kRnetBaud, SERIAL_8N1, config::kRnetRxPin, config::kRnetTxPin);
  g_cli.begin();

  const bool double_reset = g_double_reset.begin(Settings::double_reset_flag(), millis());
  Settings::set_double_reset_flag(true);
  if (double_reset) {
    LOG_WARN("double reset: opening the setup portal");
  }

  g_lock = xSemaphoreCreateMutex();
  char default_hostname[24];
  std::snprintf(default_hostname, sizeof(default_hostname), "rnet-bridge-%s", Network::id_suffix());
  g_settings.load(default_hostname);  // before the network: it sets the hostname
  g_network.begin(double_reset);
  g_cli.set_status_hook([](Print& out) { g_network.print_status(out); });

  g_api = new ApiServer(g_engine, g_model, g_network, g_settings, g_lock);  // once, at boot
  g_engine.set_frame_tap(&ApiServer::frame_tap, g_api);
  g_api->begin();
  LOG_INFO("RNET Bridge %s, proto %d, RNET on UART1 RX=GPIO%d TX=GPIO%d", config::kFirmwareVersion,
           bridge::kProtoVersion, config::kRnetRxPin, config::kRnetTxPin);
}

void loop() {
  const uint32_t now = millis();

  if (g_double_reset.should_clear(now)) {
    Settings::set_double_reset_flag(false);
  }

  g_network.loop(now);
  g_api->set_paused(g_network.portal_active());
  if (g_network.take_serving_changed() && g_network.serving() != bridge::Iface::kNone) {
    mdns::announce(g_settings.hostname, Network::id(), config::kFirmwareVersion);
    ota::begin(g_settings.hostname, g_settings.ota_password);
  }
  ota::loop();

  xSemaphoreTake(g_lock, portMAX_DELAY);
  g_engine.tick(now);
  rnet::CommandResult r{};
  while (g_engine.next_result(r)) {
    if (ApiServer::owns_result(r.id)) {
      g_api->on_result(r);
    } else {
      g_cli.on_result(r);
    }
  }
  const uint8_t changed = g_model.take_changed();
  const bool link_changed = g_engine.take_link_changed();
  if (link_changed) {
    LOG_INFO("RNET link %s", g_engine.link_up() ? "up" : "down");
  }
  g_api->loop(now, changed, link_changed);
  g_cli.poll(now, changed, link_changed);
  xSemaphoreGive(g_lock);

  if (g_api->take_restart_requested() || g_network.take_restart_requested()) {
    schedule_restart(now);
  }
  if (g_restart_pending && static_cast<int32_t>(now - g_restart_at_ms) >= 0) {
    ESP.restart();
  }
}
