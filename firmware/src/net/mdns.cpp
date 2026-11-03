#include "net/mdns.h"

#include <ESPmDNS.h>

#include "api_protocol.h"
#include "platform/log.h"

namespace mdns {

void announce(const char* hostname, const char* id, const char* fw) {
  MDNS.end();
  if (!MDNS.begin(hostname)) {
    LOG_ERROR("mdns: start failed");
    return;
  }
  MDNS.addService("rnetbridge", "tcp", 80);
  MDNS.addServiceTxt("rnetbridge", "tcp", "id", id);
  MDNS.addServiceTxt("rnetbridge", "tcp", "fw", fw);
  MDNS.addServiceTxt("rnetbridge", "tcp", "proto", String(bridge::kProtoVersion));
  LOG_INFO("mdns: announced %s.local (_rnetbridge._tcp)", hostname);
}

void stop() { MDNS.end(); }

}  // namespace mdns
