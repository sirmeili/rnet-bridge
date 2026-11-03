// mDNS advertisement `_rnetbridge._tcp` with TXT id/fw/proto (docs/protocol.md). Restarted on every
// interface change so the new address is announced (SPEC §4.4).
#pragma once

namespace mdns {

// (Re)starts the responder on the current interface.
void announce(const char* hostname, const char* id, const char* fw);
void stop();

}  // namespace mdns
