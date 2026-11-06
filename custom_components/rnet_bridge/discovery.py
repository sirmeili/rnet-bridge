"""Find a bridge again over mDNS when its stored address stops answering.

Zeroconf rediscovery normally moves the entry to a new address (config_flow), but
Home Assistant can keep a stale address: after the setup portal it retried the
bridge's old Wi-Fi address for minutes (docs/rnet-notes.md, M8 R6). This browses
`_rnetbridge._tcp` briefly and returns every address the bridge with the given id
announces, so callers can try each one.
"""

from __future__ import annotations

import asyncio
import logging

from homeassistant.components import zeroconf as ha_zeroconf
from homeassistant.core import HomeAssistant
from zeroconf import IPVersion, ServiceStateChange, Zeroconf
from zeroconf.asyncio import AsyncServiceBrowser, AsyncServiceInfo

from .const import DEFAULT_PORT

_LOGGER = logging.getLogger(__name__)

SERVICE_TYPE = "_rnetbridge._tcp.local."
BROWSE_S = 3.0
RESOLVE_MS = 3000


async def async_find_bridge(
    hass: HomeAssistant, bridge_id: str
) -> list[tuple[str, int]]:
    """Return (host, port) for each IPv4 address the bridge `bridge_id` announces."""
    aiozc = await ha_zeroconf.async_get_async_instance(hass)
    names: set[str] = set()

    # python-zeroconf calls handlers with these keyword names.
    def _on_change(
        zeroconf: Zeroconf,
        service_type: str,
        name: str,
        state_change: ServiceStateChange,
    ) -> None:
        if state_change is not ServiceStateChange.Removed:
            names.add(name)

    browser = AsyncServiceBrowser(aiozc.zeroconf, SERVICE_TYPE, handlers=[_on_change])
    try:
        await asyncio.sleep(BROWSE_S)
    finally:
        await browser.async_cancel()

    found: list[tuple[str, int]] = []
    for name in sorted(names):
        info = AsyncServiceInfo(SERVICE_TYPE, name)
        if not await info.async_request(aiozc.zeroconf, RESOLVE_MS):
            continue
        txt_id = info.properties.get(b"id")
        if txt_id is None or txt_id.decode(errors="replace") != bridge_id:
            continue
        port = info.port or DEFAULT_PORT
        found.extend((addr, port) for addr in info.parsed_addresses(IPVersion.V4Only))
    _LOGGER.debug("mDNS lookup for bridge %s found %s", bridge_id, found)
    return found
