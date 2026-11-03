#include "net/network.h"

#include <ETH.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_mac.h>

#include <cstdio>
#include <cstring>

#include "platform/log.h"

namespace {

// Olimex ESP32-POE-ISO Ethernet (SPEC §2.2): LAN8720 at address 0, PHY power GPIO12, MDC GPIO23,
// MDIO GPIO18, 50 MHz RMII clock output on GPIO17. Arduino core 2.0.17 argument order (SPEC §4.1).
constexpr uint8_t kPhyAddr = 0;
constexpr int kPhyPower = 12;
constexpr int kPhyMdc = 23;
constexpr int kPhyMdio = 18;

volatile bool g_eth_has_ip = false;
char g_id[13];
char g_id_suffix[5];

WiFiManager g_wm;
WiFiManagerParameter* g_p_hostname = nullptr;
WiFiManagerParameter* g_p_token = nullptr;
WiFiManagerParameter* g_p_ota = nullptr;
bool g_restart_requested = false;

void on_event(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_GOT_IP:
      g_eth_has_ip = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:  // core 2.0.17 has no ETH_LOST_IP event
    case ARDUINO_EVENT_ETH_STOP:
      g_eth_has_ip = false;
      break;
    default:
      break;
  }
}

void copy_param(char* dst, size_t cap, const WiFiManagerParameter* p) {
  std::strncpy(dst, p->getValue(), cap - 1);
  dst[cap - 1] = '\0';
}

}  // namespace

const char* Network::id() { return g_id; }
const char* Network::id_suffix() { return g_id_suffix; }

void Network::begin(bool force_portal) {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_ETH);
  std::snprintf(g_id, sizeof(g_id), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3],
                mac[4], mac[5]);
  std::snprintf(g_id_suffix, sizeof(g_id_suffix), "%02x%02x", mac[4], mac[5]);

  WiFi.onEvent(on_event);
  WiFi.persistent(true);
  // The saved credentials can only be read with the Wi-Fi driver started. Read them once here: a
  // portal save restarts the board, so this can't go stale.
  WiFi.mode(WIFI_STA);
  has_credentials_ = WiFi.SSID().length() > 0;
  WiFi.mode(WIFI_OFF);
  WiFi.setAutoReconnect(true);
  ETH.begin(kPhyAddr, kPhyPower, kPhyMdc, kPhyMdio, ETH_PHY_LAN8720, ETH_CLOCK_GPIO17_OUT);
  ETH.setHostname(settings_.hostname);

  policy_ = new bridge::NetworkPolicy(force_portal);  // once at boot: not in the RX/TX path
  LOG_INFO("network: id %s, hostname %s, Wi-Fi %s%s", g_id, settings_.hostname,
           has_credentials_ ? "saved" : "not set", force_portal ? ", portal forced" : "");
}

void Network::loop(uint32_t now_ms) {
  const bridge::NetInputs in{g_eth_has_ip, sta_on_ && WiFi.status() == WL_CONNECTED,
                             has_credentials_};
  const bridge::NetPlan plan = policy_->update(now_ms, in);
  apply(plan);
  if (portal_running_) {
    g_wm.process();
  }
}

void Network::apply(const bridge::NetPlan& plan) {
  if (plan.portal && !portal_running_) {
    start_portal();
  } else if (!plan.portal && portal_running_) {
    stop_portal();
  }

  if (plan.wifi_sta && !sta_on_) {
    LOG_INFO("network: starting Wi-Fi");
    WiFi.setHostname(settings_.hostname);
    WiFi.mode(portal_running_ ? WIFI_AP_STA : WIFI_STA);
    WiFi.begin();  // saved credentials
    sta_on_ = true;
  } else if (!plan.wifi_sta && sta_on_) {
    LOG_INFO("network: stopping Wi-Fi");
    WiFi.disconnect(false, false);  // keep the saved credentials
    if (!portal_running_) {
      WiFi.mode(WIFI_OFF);
    }
    sta_on_ = false;
  }

  if (plan.serving != serving_) {
    serving_ = plan.serving;
    LOG_INFO("network: serving on %s, IP %s",
             serving_ == bridge::Iface::kEthernet ? "Ethernet"
             : serving_ == bridge::Iface::kWifi   ? "Wi-Fi"
                                                  : "nothing",
             local_ip().c_str());
  }
}

void Network::start_portal() {
  char ap[24];
  std::snprintf(ap, sizeof(ap), "RNET-Bridge-%s", g_id_suffix);
  for (char* c = ap + 12; *c != '\0'; ++c) {
    *c = static_cast<char>(toupper(*c));
  }
  if (g_p_hostname == nullptr) {
    // Created once and kept: WiFiManager holds pointers to them.
    g_p_hostname = new WiFiManagerParameter("hostname", "Hostname", settings_.hostname,
                                            sizeof(settings_.hostname) - 1);
    g_p_token = new WiFiManagerParameter("token", "API token (optional)", settings_.token,
                                         bridge::kMaxTokenLen);
    g_p_ota = new WiFiManagerParameter("ota", "OTA password (optional)", settings_.ota_password,
                                       bridge::kMaxTokenLen);
    g_wm.addParameter(g_p_hostname);
    g_wm.addParameter(g_p_token);
    g_wm.addParameter(g_p_ota);
    g_wm.setSaveParamsCallback([this]() {
      copy_param(settings_.hostname, sizeof(settings_.hostname), g_p_hostname);
      copy_param(settings_.token, sizeof(settings_.token), g_p_token);
      copy_param(settings_.ota_password, sizeof(settings_.ota_password), g_p_ota);
      if (settings_.hostname[0] == '\0') {
        std::snprintf(settings_.hostname, sizeof(settings_.hostname), "rnet-bridge-%s",
                      g_id_suffix);
      }
      settings_.save();
      g_restart_requested = true;
    });
    g_wm.setSaveConfigCallback([]() { g_restart_requested = true; });
  }
  g_wm.setConfigPortalBlocking(false);
  g_wm.setConfigPortalTimeout(0);  // NetworkPolicy decides when it closes
  g_wm.setEnableConfigPortal(true);
  LOG_WARN("network: opening setup portal (AP %s); API paused", ap);
  g_wm.startConfigPortal(ap);
  portal_running_ = true;
}

void Network::stop_portal() {
  LOG_INFO("network: closing setup portal");
  g_wm.stopConfigPortal();
  if (!sta_on_) {
    WiFi.mode(WIFI_OFF);
  }
  portal_running_ = false;
  policy_->portal_done();
}

bool Network::take_restart_requested() {
  const bool r = g_restart_requested;
  g_restart_requested = false;
  return r;
}

int Network::wifi_rssi() const { return WiFi.RSSI(); }

String Network::local_ip() const {
  switch (serving_) {
    case bridge::Iface::kEthernet:
      return ETH.localIP().toString();
    case bridge::Iface::kWifi:
      return WiFi.localIP().toString();
    case bridge::Iface::kNone:
      break;
  }
  return String("-");
}
