"""Config entry diagnostics: bridge info, link stats, zone states (SPEC §5.2)."""

from __future__ import annotations

from dataclasses import asdict
from typing import Any

from homeassistant.components.diagnostics import async_redact_data
from homeassistant.const import CONF_HOST, CONF_PORT, CONF_TOKEN
from homeassistant.core import HomeAssistant
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from . import RnetConfigEntry
from .api import RnetBridgeError, async_get_info
from .const import DEFAULT_PORT

TO_REDACT = {CONF_TOKEN}


async def async_get_config_entry_diagnostics(
    hass: HomeAssistant, entry: RnetConfigEntry
) -> dict[str, Any]:
    """Return diagnostics for a config entry. The API token is redacted."""
    coordinator = entry.runtime_data
    data = coordinator.data
    info = coordinator.client.info

    bridge_info: dict[str, Any]
    try:
        bridge_info = await async_get_info(
            async_get_clientsession(hass),
            entry.data[CONF_HOST],
            entry.data.get(CONF_PORT, DEFAULT_PORT),
        )
    except RnetBridgeError as err:
        bridge_info = {"error": str(err)}

    return {
        "entry": {
            "data": async_redact_data(dict(entry.data), TO_REDACT),
            "options": dict(entry.options),
        },
        "hello": None if info is None else asdict(info),
        "api_info": bridge_info,
        "connected": data.connected,
        "link": {"up": data.link.up, "stats": data.link.stats},
        "zones": {str(n): asdict(z) for n, z in sorted(data.zones.items())},
    }
