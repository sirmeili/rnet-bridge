"""The Russound RNET Bridge integration (SPEC §5)."""

from __future__ import annotations

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_HOST, CONF_PORT, CONF_TOKEN, Platform
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import ConfigEntryError, ConfigEntryNotReady
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .api import (
    RnetBridgeAuthError,
    RnetBridgeClient,
    RnetBridgeConnectionError,
    RnetBridgeProtocolError,
)
from .const import (
    CONTROLLER_MODEL,
    CONTROLLER_NAME,
    DEFAULT_PORT,
    DOMAIN,
    MANUFACTURER,
    SUPPORTED_PROTO,
)
from .coordinator import RnetBridgeCoordinator

# Entity platforms arrive in M6 (SPEC §9).
PLATFORMS: list[Platform] = []

type RnetConfigEntry = ConfigEntry[RnetBridgeCoordinator]


async def async_setup_entry(hass: HomeAssistant, entry: RnetConfigEntry) -> bool:
    """Connect to the bridge, register the controller device, start listening."""
    host: str = entry.data[CONF_HOST]
    port: int = entry.data.get(CONF_PORT, DEFAULT_PORT)
    client = RnetBridgeClient(
        async_get_clientsession(hass), host, port, entry.data.get(CONF_TOKEN)
    )
    coordinator = RnetBridgeCoordinator(hass, entry, client)
    try:
        await coordinator.async_connect()
    except RnetBridgeConnectionError as err:
        raise ConfigEntryNotReady(
            translation_domain=DOMAIN,
            translation_key="cannot_connect",
            translation_placeholders={"host": host},
        ) from err
    except RnetBridgeAuthError as err:
        # Reauth arrives in M7; until then the user re-adds the bridge.
        raise ConfigEntryError(
            translation_domain=DOMAIN, translation_key="invalid_auth"
        ) from err
    except RnetBridgeProtocolError as err:
        # The repair issue for this arrives in M7 (SPEC §4.5).
        raise ConfigEntryError(
            translation_domain=DOMAIN,
            translation_key="unsupported_proto",
            translation_placeholders={
                "proto": str(err.proto),
                "supported": str(SUPPORTED_PROTO),
            },
        ) from err

    entry.runtime_data = coordinator

    dr.async_get(hass).async_get_or_create(
        config_entry_id=entry.entry_id,
        identifiers={(DOMAIN, coordinator.bridge_id)},
        manufacturer=MANUFACTURER,
        model=CONTROLLER_MODEL,
        name=CONTROLLER_NAME,
        sw_version=coordinator.firmware,
        configuration_url=f"http://{host}:{port}/" if port != 80 else f"http://{host}/",
    )

    coordinator.start()
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: RnetConfigEntry) -> bool:
    """Unload platforms and close the connection."""
    unloaded = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unloaded:
        await entry.runtime_data.async_shutdown()
    return unloaded
