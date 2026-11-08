"""The Russound RNET Bridge integration (SPEC §5)."""

from __future__ import annotations

import logging

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_HOST, CONF_PORT, CONF_TOKEN, Platform
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import (
    ConfigEntryAuthFailed,
    ConfigEntryError,
    ConfigEntryNotReady,
)
from homeassistant.helpers import config_validation as cv, device_registry as dr
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.typing import ConfigType

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
    NUM_ZONES,
    SUPPORTED_PROTO,
)
from .coordinator import (
    RnetBridgeCoordinator,
    async_create_proto_issue,
    async_delete_proto_issue,
)
from .discovery import async_find_bridge
from .entity import controller_identifier, zone_enabled, zone_identifier
from .services import async_setup_services

PLATFORMS: list[Platform] = [
    Platform.BINARY_SENSOR,
    Platform.BUTTON,
    Platform.MEDIA_PLAYER,
    Platform.NUMBER,
    Platform.SENSOR,
    Platform.SWITCH,
]

CONFIG_SCHEMA = cv.config_entry_only_config_schema(DOMAIN)

_LOGGER = logging.getLogger(__name__)

type RnetConfigEntry = ConfigEntry[RnetBridgeCoordinator]


async def async_setup(hass: HomeAssistant, config: ConfigType) -> bool:
    """Register the services once (not per entry)."""
    async_setup_services(hass)
    return True


async def async_setup_entry(hass: HomeAssistant, entry: RnetConfigEntry) -> bool:
    """Connect to the bridge, register the controller device, start listening."""
    host: str = entry.data[CONF_HOST]
    port: int = entry.data.get(CONF_PORT, DEFAULT_PORT)
    try:
        coordinator, host, port = await _async_first_connect(hass, entry, host, port)
    except RnetBridgeConnectionError as err:
        raise ConfigEntryNotReady(
            translation_domain=DOMAIN,
            translation_key="cannot_connect",
            translation_placeholders={"host": host},
        ) from err
    except RnetBridgeAuthError as err:
        # Starts the reauth flow (config_flow.async_step_reauth).
        raise ConfigEntryAuthFailed(
            translation_domain=DOMAIN, translation_key="invalid_auth"
        ) from err
    except RnetBridgeProtocolError as err:
        # Firmware and integration must be updated together (SPEC §4.5).
        async_create_proto_issue(hass, entry, err.proto)
        raise ConfigEntryError(
            translation_domain=DOMAIN,
            translation_key="unsupported_proto",
            translation_placeholders={
                "proto": str(err.proto),
                "supported": str(SUPPORTED_PROTO),
            },
        ) from err

    async_delete_proto_issue(hass, entry)
    entry.runtime_data = coordinator

    controller = dr.async_get(hass).async_get_or_create(
        config_entry_id=entry.entry_id,
        identifiers={controller_identifier(coordinator.bridge_id)},
        manufacturer=MANUFACTURER,
        model=CONTROLLER_MODEL,
        name=CONTROLLER_NAME,
        sw_version=coordinator.firmware,
        configuration_url=f"http://{host}:{port}/" if port != 80 else f"http://{host}/",
    )

    coordinator.controller_device_id = controller.id
    _async_remove_stale_devices(hass, entry, coordinator.bridge_id)

    coordinator.start()
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    # The options flow changes zones/sources: reload to rebuild entities (SPEC §5.3).
    entry.async_on_unload(entry.add_update_listener(_async_options_updated))
    return True


async def _async_connect(
    hass: HomeAssistant, entry: RnetConfigEntry, host: str, port: int
) -> RnetBridgeCoordinator:
    client = RnetBridgeClient(
        async_get_clientsession(hass), host, port, entry.data.get(CONF_TOKEN)
    )
    coordinator = RnetBridgeCoordinator(hass, entry, client)
    await coordinator.async_connect()
    return coordinator


async def _async_first_connect(
    hass: HomeAssistant, entry: RnetConfigEntry, host: str, port: int
) -> tuple[RnetBridgeCoordinator, str, int]:
    """Connect at the stored address; if it doesn't answer, look the bridge up
    over mDNS and try the addresses it announces. A found address is saved."""
    try:
        return await _async_connect(hass, entry, host, port), host, port
    except RnetBridgeConnectionError:
        if entry.unique_id is None:
            raise
        for new_host, new_port in await async_find_bridge(hass, entry.unique_id):
            if (new_host, new_port) == (host, port):
                continue
            try:
                coordinator = await _async_connect(hass, entry, new_host, new_port)
            except RnetBridgeConnectionError:
                continue
            _LOGGER.info(
                "RNET Bridge found at %s:%s (was %s:%s)", new_host, new_port, host, port
            )
            hass.config_entries.async_update_entry(
                entry, data={**entry.data, CONF_HOST: new_host, CONF_PORT: new_port}
            )
            return coordinator, new_host, new_port
        raise


async def _async_options_updated(hass: HomeAssistant, entry: RnetConfigEntry) -> None:
    await hass.config_entries.async_reload(entry.entry_id)


def _async_remove_stale_devices(
    hass: HomeAssistant, entry: RnetConfigEntry, bridge_id: str
) -> None:
    """Remove zone devices for zones that are now disabled (SPEC §5.3)."""
    registry = dr.async_get(hass)
    wanted = {controller_identifier(bridge_id)} | {
        zone_identifier(bridge_id, z)
        for z in range(1, NUM_ZONES + 1)
        if zone_enabled(entry.options, z)
    }
    for device in dr.async_entries_for_config_entry(registry, entry.entry_id):
        if not device.identifiers & wanted:
            registry.async_update_device(
                device.id, remove_config_entry_id=entry.entry_id
            )


async def async_unload_entry(hass: HomeAssistant, entry: RnetConfigEntry) -> bool:
    """Unload platforms and close the connection."""
    unloaded = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unloaded:
        await entry.runtime_data.async_shutdown()
    return unloaded
