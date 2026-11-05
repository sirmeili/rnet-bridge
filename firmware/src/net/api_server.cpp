#include "net/api_server.h"

#include <Update.h>

#include <cstring>

#include "config.h"
#include "platform/log.h"

namespace {

constexpr uint32_t kAuthTimeoutMs = 5000;
constexpr uint32_t kPingIntervalMs = 15000;
constexpr uint32_t kClientTimeoutMs = 30000;
constexpr uint32_t kLinkIntervalMs = 30000;
constexpr uint32_t kCleanupIntervalMs = 1000;

ApiServer* g_instance = nullptr;

const char* yes_no(bool b) { return b ? "yes" : "no"; }

}  // namespace

ApiServer::ApiServer(rnet::Engine& engine, rnet::ZoneModel& model, Network& network,
                     Settings& settings, SemaphoreHandle_t state_lock)
    : engine_(engine), model_(model), network_(network), settings_(settings), lock_(state_lock) {}

void ApiServer::begin() {
  g_instance = this;
  events_ = xQueueCreate(8, sizeof(Event));

  ws_.onEvent([this](AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void* arg,
                     uint8_t* data, size_t len) { on_ws_event(client, type, arg, data, len); });
  server_.addHandler(&ws_);

  server_.on("/api/info", HTTP_GET, [this](AsyncWebServerRequest* request) {
    char body[384];
    const bridge::BridgeInfo i = info();
    bridge::encode_info(i, body, sizeof(body));
    request->send(200, "application/json", body);
  });

  server_.on("/api/factory_reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!authorized(request, settings_.token)) {
      request->send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
      return;
    }
    request->send(200, "application/json", "{\"ok\":true}");
    factory_reset_requested_ = true;  // done in the main loop, not on the async_tcp task
  });

  server_.on(
      "/api/ota", HTTP_POST,
      [this](AsyncWebServerRequest* request) {
        if (!ota_authorized_) {
          request->send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
        } else if (!ota_ok_) {
          request->send(500, "application/json",
                        "{\"ok\":false,\"error\":\"update failed\"}");
        } else {
          request->send(200, "application/json", "{\"ok\":true}");
          restart_requested_ = true;
        }
      },
      [this](AsyncWebServerRequest* request, const String&, size_t index, uint8_t* data,
             size_t len, bool final) { handle_ota_upload(request, index, data, len, final); });

  server_.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) { handle_status(request); });
  server_.onNotFound([](AsyncWebServerRequest* request) { request->send(404); });
  server_.begin();
}

void ApiServer::set_paused(bool paused) {
  if (paused == paused_) {
    return;
  }
  paused_ = paused;
  if (paused) {
    ws_.closeAll();
    server_.end();
    LOG_WARN("api: paused for the setup portal");
  } else {
    server_.begin();
    LOG_INFO("api: serving again");
  }
}

bool ApiServer::take_restart_requested() {
  if (factory_reset_requested_) {
    factory_reset_requested_ = false;
    LOG_WARN("api: factory reset: erasing settings and Wi-Fi credentials");
    Settings::factory_reset();
    return true;
  }
  const bool r = restart_requested_;
  restart_requested_ = false;
  return r;
}

bool ApiServer::authorized(AsyncWebServerRequest* request, const char* secret) const {
  if (secret[0] == '\0') {
    return true;
  }
  const AsyncWebHeader* h = request->getHeader("Authorization");
  if (h == nullptr) {
    return false;
  }
  String expected = String("Bearer ") + secret;
  return h->value() == expected;
}

void ApiServer::handle_ota_upload(AsyncWebServerRequest* request, size_t index, uint8_t* data,
                                  size_t len, bool final) {
  if (index == 0) {
    ota_authorized_ = authorized(request, settings_.ota_password);
    ota_ok_ = false;
    if (!ota_authorized_) {
      LOG_WARN("api: OTA rejected (unauthorized)");
      return;
    }
    LOG_INFO("api: OTA upload started");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      LOG_ERROR("api: OTA begin failed: %s", Update.errorString());
      return;
    }
  }
  if (!ota_authorized_ || Update.hasError()) {
    return;
  }
  if (Update.write(data, len) != len) {
    LOG_ERROR("api: OTA write failed: %s", Update.errorString());
    return;
  }
  if (final) {
    ota_ok_ = Update.end(true);
    if (ota_ok_) {
      LOG_INFO("api: OTA complete, %u bytes; restarting", static_cast<unsigned>(index + len));
    } else {
      LOG_ERROR("api: OTA end failed: %s", Update.errorString());
    }
  }
}

bridge::BridgeInfo ApiServer::info() const {
  return bridge::BridgeInfo{Network::id(), settings_.hostname, config::kFirmwareVersion,
                            static_cast<uint32_t>(millis() / 1000), network_.serving(),
                            network_.wifi_rssi()};
}

// --- WebSocket ---

void ApiServer::on_ws_event(AsyncWebSocketClient* client, AwsEventType type, void* arg,
                            uint8_t* data, size_t len) {
  // async_tcp task: copy and hand over to the main loop.
  Event e{};
  e.client = client->id();
  switch (type) {
    case WS_EVT_CONNECT:
      e.kind = EventKind::kConnect;
      break;
    case WS_EVT_DISCONNECT:
      e.kind = EventKind::kDisconnect;
      break;
    case WS_EVT_PONG:
      e.kind = EventKind::kPong;
      break;
    case WS_EVT_DATA: {
      const auto* fi = static_cast<AwsFrameInfo*>(arg);
      // Only whole, single-frame text messages; anything else is answered with bad_message.
      if (fi->opcode == WS_TEXT && fi->final && fi->index == 0 && fi->len == len &&
          len < kMaxMessage) {
        e.kind = EventKind::kText;
        e.len = static_cast<uint16_t>(len);
        std::memcpy(e.data, data, len);
      } else if (fi->final) {
        e.kind = EventKind::kTooBig;
      } else {
        return;
      }
      break;
    }
    default:
      return;
  }
  if (xQueueSend(events_, &e, 0) != pdTRUE && type == WS_EVT_CONNECT) {
    client->close();  // can't track it
  }
}

ApiServer::Client* ApiServer::find(uint32_t id) {
  for (Client& c : clients_) {
    if (c.used && c.id == id) {
      return &c;
    }
  }
  return nullptr;
}

void ApiServer::loop(uint32_t now_ms, uint8_t changed_zones, bool link_changed) {
  Event e;
  while (xQueueReceive(events_, &e, 0) == pdTRUE) {
    handle_event(e, now_ms);
  }
  if (paused_) {
    return;
  }

  for (uint8_t z = 1; z <= rnet::kNumZones; ++z) {
    if ((changed_zones & (1u << (z - 1))) && model_.zone(z).valid) {
      broadcast(bridge::encode_zone_state(z, model_.zone(z), buf_, sizeof(buf_)));
    }
  }
  if (link_changed || now_ms - last_link_ms_ >= kLinkIntervalMs) {
    last_link_ms_ = now_ms;
    broadcast(bridge::encode_link(engine_, buf_, sizeof(buf_)));
  }

  for (Client& c : clients_) {
    if (!c.used) {
      continue;
    }
    if (!c.authed && now_ms - c.connected_ms >= kAuthTimeoutMs) {
      send_error(c.id, "unauthorized");
      ws_.close(c.id);
      c.used = false;
      continue;
    }
    if (now_ms - c.last_seen_ms >= kClientTimeoutMs) {
      LOG_INFO("api: client %lu timed out", static_cast<unsigned long>(c.id));
      ws_.close(c.id);
      c.used = false;
      continue;
    }
    forward_logs(c);
  }

  if (now_ms - last_ping_ms_ >= kPingIntervalMs) {
    last_ping_ms_ = now_ms;
    ws_.pingAll();
  }
  if (now_ms - last_cleanup_ms_ >= kCleanupIntervalMs) {
    last_cleanup_ms_ = now_ms;
    // Only frees closed clients. With a limit, the library closes the *oldest* client when over it,
    // which dropped long-lived clients when a Home Assistant restart briefly left a half-open
    // socket next to the new one (M8 R1). The limit is ours: handle_event refuses a client when
    // every slot is taken, and a stale client leaves after kClientTimeoutMs.
    ws_.cleanupClients(UINT16_MAX);
  }
}

void ApiServer::handle_event(const Event& e, uint32_t now_ms) {
  Client* c = find(e.client);
  switch (e.kind) {
    case EventKind::kConnect: {
      Client* slot = nullptr;
      for (Client& s : clients_) {
        if (!s.used) {
          slot = &s;
          break;
        }
      }
      if (slot == nullptr || paused_) {
        ws_.close(e.client);
        return;
      }
      *slot = Client{true,         e.client, settings_.token[0] == '\0', now_ms, now_ms,
                     bridge::LogLevel::kOff, false, 0};
      bridge::encode_hello(info(), !slot->authed, buf_, sizeof(buf_));
      send(*slot, std::strlen(buf_));
      if (slot->authed) {
        send_snapshot(*slot);
      }
      LOG_INFO("api: client %lu connected", static_cast<unsigned long>(e.client));
      return;
    }
    case EventKind::kDisconnect:
      if (c != nullptr) {
        c->used = false;
        LOG_INFO("api: client %lu disconnected", static_cast<unsigned long>(e.client));
      }
      return;
    case EventKind::kPong:
      if (c != nullptr) {
        c->last_seen_ms = now_ms;
      }
      return;
    case EventKind::kTooBig:
      if (c != nullptr) {
        c->last_seen_ms = now_ms;
        send_error(c->id, "bad_message");
      }
      return;
    case EventKind::kText:
      if (c != nullptr) {
        c->last_seen_ms = now_ms;
        handle_message(*c, e.data, e.len, now_ms);
      }
      return;
  }
}

void ApiServer::handle_message(Client& c, const char* text, size_t len, uint32_t now_ms) {
  (void)now_ms;
  const bridge::ClientMsg m = bridge::parse_client_message(text, len);
  if (m.type == bridge::ClientMsgType::kBad) {
    send_error(c.id, "bad_message");
    return;
  }
  if (!c.authed) {
    if (m.type == bridge::ClientMsgType::kAuth && m.valid &&
        std::strcmp(m.token, settings_.token) == 0) {
      c.authed = true;
      send_snapshot(c);
    } else {
      LOG_WARN("api: client %lu failed auth", static_cast<unsigned long>(c.id));
      send_error(c.id, "unauthorized");
      ws_.close(c.id);
      c.used = false;
    }
    return;
  }

  switch (m.type) {
    case bridge::ClientMsgType::kAuth:
      return;  // already authorized
    case bridge::ClientMsgType::kCommand:
      if (!m.valid) {
        if (m.has_id) {
          send(c, bridge::encode_result(m.id, rnet::CommandError::kInvalid, buf_, sizeof(buf_)));
        } else {
          send_error(c.id, "bad_message");
        }
        return;
      }
      submit(c, m.id, &m.command, nullptr, 0);
      return;
    case bridge::ClientMsgType::kRawSend:
      if (!m.valid) {
        if (m.has_id) {
          send(c, bridge::encode_result(m.id, rnet::CommandError::kInvalid, buf_, sizeof(buf_)));
        } else {
          send_error(c.id, "bad_message");
        }
        return;
      }
      submit(c, m.id, nullptr, m.raw, m.raw_len);
      return;
    case bridge::ClientMsgType::kSubscribeLogs:
      if (m.valid) {
        c.log_level = m.log_level;
        c.log_cursor = g_log.next_seq();
      } else {
        send_error(c.id, "bad_message");
      }
      return;
    case bridge::ClientMsgType::kTraceFrames:
      if (m.valid) {
        c.trace = m.trace_enabled;
      } else {
        send_error(c.id, "bad_message");
      }
      return;
    case bridge::ClientMsgType::kBad:
      return;
  }
}

void ApiServer::submit(Client& c, uint32_t client_msg_id, const rnet::Command* cmd,
                       const uint8_t* raw, size_t raw_len) {
  Pending* slot = nullptr;
  for (Pending& p : pending_) {
    if (!p.used) {
      slot = &p;
      break;
    }
  }
  if (slot == nullptr) {
    send(c, bridge::encode_result(client_msg_id, rnet::CommandError::kBusy, buf_, sizeof(buf_)));
    return;
  }
  const uint32_t internal = next_internal_id_;
  next_internal_id_ = (next_internal_id_ + 1) & ~kCliIdBit;
  if (next_internal_id_ == 0) {
    next_internal_id_ = 1;
  }
  rnet::CommandError err;
  if (cmd != nullptr) {
    rnet::Command copy = *cmd;
    copy.id = internal;
    err = engine_.submit(copy);
  } else {
    err = engine_.submit_raw(internal, raw, raw_len);
  }
  if (err != rnet::CommandError::kNone) {
    send(c, bridge::encode_result(client_msg_id, err, buf_, sizeof(buf_)));
    return;
  }
  *slot = Pending{true, internal, c.id, client_msg_id};
}

void ApiServer::on_result(const rnet::CommandResult& r) {
  for (Pending& p : pending_) {
    if (p.used && p.internal_id == r.id) {
      p.used = false;
      Client* c = find(p.client);
      if (c != nullptr && !paused_) {
        send(*c, bridge::encode_result(p.client_msg_id, r.error, buf_, sizeof(buf_)));
      }
      return;
    }
  }
}

void ApiServer::send_snapshot(Client& c) {
  send(c, bridge::encode_snapshot(engine_, model_, buf_, sizeof(buf_)));
}

void ApiServer::send(const Client& c, size_t len) {
  if (len > 0) {
    ws_.text(c.id, buf_, len);
  }
}

void ApiServer::send_error(uint32_t client, const char* error) {
  const size_t len = bridge::encode_error(error, buf_, sizeof(buf_));
  if (len > 0) {
    ws_.text(client, buf_, len);
  }
}

void ApiServer::broadcast(size_t len) {
  if (len == 0) {
    return;
  }
  for (const Client& c : clients_) {
    if (c.used && c.authed) {
      ws_.text(c.id, buf_, len);
    }
  }
}

void ApiServer::forward_logs(Client& c) {
  if (!c.authed || c.log_level == bridge::LogLevel::kOff) {
    c.log_cursor = g_log.next_seq();
    return;
  }
  while (c.log_cursor < g_log.next_seq()) {
    const Log::Line* l = g_log.line(c.log_cursor);
    ++c.log_cursor;
    if (l != nullptr && l->level >= c.log_level) {
      send(c, bridge::encode_log(l->level, l->text, buf_, sizeof(buf_)));
    }
  }
}

void ApiServer::frame_tap(void* ctx, bool tx, const uint8_t* wire, size_t len) {
  auto* self = static_cast<ApiServer*>(ctx);
  if (self->paused_) {
    return;
  }
  size_t n = 0;
  for (const Client& c : self->clients_) {
    if (c.used && c.authed && c.trace) {
      if (n == 0) {
        n = bridge::encode_frame(tx, wire, len, self->buf_, sizeof(self->buf_));
      }
      if (n > 0) {
        self->ws_.text(c.id, self->buf_, n);
      }
    }
  }
}

// --- Status page (not part of the contract) ---

void ApiServer::handle_status(AsyncWebServerRequest* request) {
  if (xSemaphoreTake(lock_, pdMS_TO_TICKS(200)) != pdTRUE) {
    request->send(503, "text/plain", "busy, try again");
    return;
  }
  String html;
  html.reserve(4096);
  html += "<!doctype html><html><head><meta charset=utf-8><meta name=viewport "
          "content='width=device-width'><title>RNET Bridge</title><style>body{font-family:sans-serif;"
          "margin:16px}td,th{padding:2px 8px;text-align:right}th{background:#eee}</style></head><body>";
  html += "<h2>RNET Bridge ";
  html += settings_.hostname;
  html += "</h2><p>id ";
  html += Network::id();
  html += " &middot; fw ";
  html += config::kFirmwareVersion;
  html += " &middot; proto ";
  html += bridge::kProtoVersion;
  html += " &middot; ";
  html += network_.serving() == bridge::Iface::kWifi ? "Wi-Fi" : "Ethernet";
  html += " ";
  html += network_.local_ip();
  html += " &middot; up ";
  html += static_cast<unsigned long>(millis() / 1000);
  html += " s</p><p>RNET link: <b>";
  html += engine_.link_up() ? "UP" : "DOWN";
  html += "</b></p><table><tr><th>Zone</th><th>Power</th><th>Source</th><th>Volume</th><th>Muted</th>"
          "<th>Bass</th><th>Treble</th><th>Balance</th><th>Loudness</th><th>Turn-on</th></tr>";
  for (uint8_t z = 1; z <= rnet::kNumZones; ++z) {
    const rnet::ZoneState& s = model_.zone(z);
    html += "<tr><td>";
    html += z;
    if (!s.valid) {
      html += "</td><td colspan=9>not polled yet</td></tr>";
      continue;
    }
    html += "</td><td>";
    html += s.power ? "on" : "off";
    html += "</td><td>";
    html += s.source;
    html += "</td><td>";
    html += s.volume;
    html += "</td><td>";
    html += yes_no(s.muted);
    html += "</td><td>";
    html += s.bass;
    html += "</td><td>";
    html += s.treble;
    html += "</td><td>";
    html += s.balance;
    html += "</td><td>";
    html += s.loudness ? "on" : "off";
    html += "</td><td>";
    html += s.turn_on_volume_valid ? String(s.turn_on_volume) : String("?");
    html += "</td></tr>";
  }
  const rnet::EngineStats& st = engine_.stats();
  html += "</table><p>tx ";
  html += st.tx;
  html += " &middot; rx ";
  html += st.rx;
  html += " &middot; checksum errors ";
  html += st.checksum_errors;
  html += " &middot; dropped ";
  html += st.dropped;
  html += " &middot; timeouts ";
  html += st.timeouts;
  html += " &middot; retries ";
  html += st.retries;
  html += " &middot; duplicate replies ";
  html += st.duplicate_replies;
  html += "</p></body></html>";
  xSemaphoreGive(lock_);
  request->send(200, "text/html", html);
}
