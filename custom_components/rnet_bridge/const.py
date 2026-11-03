"""Constants for the Russound RNET Bridge integration."""

from __future__ import annotations

from typing import Final

DOMAIN: Final = "rnet_bridge"

# docs/protocol.md
SUPPORTED_PROTO: Final = 1
DEFAULT_PORT: Final = 80
NUM_ZONES: Final = 6
NUM_SOURCES: Final = 6

CONF_ZONES: Final = "zones"
CONF_SOURCES: Final = "sources"
CONF_ENABLED: Final = "enabled"

# Commands wait this long for their `result` (SPEC §5.4).
COMMAND_TIMEOUT_S: Final = 5.0
CONNECT_TIMEOUT_S: Final = 10.0
# The bridge sends `link` at least every 30 s (docs/protocol.md); silence this long
# means the connection is dead.
LISTEN_TIMEOUT_S: Final = 75.0

# Reconnect backoff (SPEC §5.2).
RECONNECT_MIN_S: Final = 1.0
RECONNECT_MAX_S: Final = 60.0

MANUFACTURER: Final = "Russound"
CONTROLLER_NAME: Final = "Russound Controller"
CONTROLLER_MODEL: Final = "RNET Bridge"
