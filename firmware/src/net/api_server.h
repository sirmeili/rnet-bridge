// HTTP and WebSocket API, proto 1 (docs/protocol.md, SPEC §4.5).
//
// Threading: ESPAsyncWebServer runs its callbacks on the async_tcp task, but the engine and model
// belong to the main loop. WebSocket events are copied onto a FreeRTOS queue and handled in loop().
// HTTP handlers only read; the one that reads engine/model state (the status page) takes the state
// lock, which the main loop holds while it ticks the engine and calls loop().
#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "api_protocol.h"
#include "net/network.h"
#include "net/settings.h"
#include "rnet_engine.h"
#include "zone_model.h"

class ApiServer {
 public:
  static constexpr size_t kMaxClients = 4;
  static constexpr size_t kMaxMessage = 512;  // largest client message accepted

  ApiServer(rnet::Engine& engine, rnet::ZoneModel& model, Network& network, Settings& settings,
            SemaphoreHandle_t state_lock);

  void begin();
  // Call from the main loop with the state lock held.
  void loop(uint32_t now_ms, uint8_t changed_zones, bool link_changed);
  // A command result whose id this server issued (see owns_result).
  void on_result(const rnet::CommandResult& r);
  static bool owns_result(uint32_t id) { return (id & kCliIdBit) == 0; }
  // The portal needs port 80: stop serving while it is open.
  void set_paused(bool paused);
  // Closes every WebSocket client with a close frame and refuses new ones, so clients notice a
  // restart at once instead of through a timeout. Called when a restart is scheduled.
  void close_all();
  // True once after OTA succeeded or a factory reset was requested: the caller restarts. A factory
  // reset's erase runs here, in the main loop. Call from the main loop only.
  bool take_restart_requested();
  // Engine frame tap (Engine::set_frame_tap) for trace_frames.
  static void frame_tap(void* ctx, bool tx, const uint8_t* wire, size_t len);

  // USB CLI command ids have this bit set, so results go back to the right owner.
  static constexpr uint32_t kCliIdBit = 0x80000000u;

 private:
  enum class EventKind : uint8_t { kConnect, kDisconnect, kText, kTooBig, kPong };
  struct Event {
    uint32_t client;
    EventKind kind;
    uint16_t len;
    char data[kMaxMessage];
  };
  struct Client {
    bool used;
    uint32_t id;
    bool authed;
    uint32_t connected_ms;
    uint32_t last_seen_ms;
    bridge::LogLevel log_level;
    bool trace;
    uint32_t log_cursor;
  };
  struct Pending {
    bool used;
    uint32_t internal_id;
    uint32_t client;
    uint32_t client_msg_id;
  };

  void on_ws_event(AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data,
                   size_t len);
  void handle_event(const Event& e, uint32_t now_ms);
  void handle_message(Client& c, const char* text, size_t len, uint32_t now_ms);
  void submit(Client& c, uint32_t client_msg_id, const rnet::Command* cmd, const uint8_t* raw,
              size_t raw_len);
  void send_snapshot(Client& c);
  void send(const Client& c, size_t len);
  void send_error(uint32_t client, const char* error);
  void broadcast(size_t len);
  void forward_logs(Client& c);
  Client* find(uint32_t id);
  bridge::BridgeInfo info() const;
  bool authorized(AsyncWebServerRequest* request, const char* secret) const;
  void handle_status(AsyncWebServerRequest* request);
  void handle_ota_upload(AsyncWebServerRequest* request, size_t index, uint8_t* data, size_t len,
                         bool final);

  rnet::Engine& engine_;
  rnet::ZoneModel& model_;
  Network& network_;
  Settings& settings_;
  SemaphoreHandle_t lock_;
  AsyncWebServer server_{80};
  AsyncWebSocket ws_{"/ws"};
  QueueHandle_t events_ = nullptr;
  bool paused_ = false;
  bool closing_ = false;  // a restart is pending: refuse new clients

  Client clients_[kMaxClients] = {};
  Pending pending_[64] = {};
  uint32_t next_internal_id_ = 1;
  uint32_t last_ping_ms_ = 0;
  uint32_t last_link_ms_ = 0;
  uint32_t last_cleanup_ms_ = 0;
  volatile bool restart_requested_ = false;
  volatile bool factory_reset_requested_ = false;
  volatile bool ota_authorized_ = false;
  volatile bool ota_ok_ = false;

  char buf_[2048];  // outgoing message, main loop only
};
