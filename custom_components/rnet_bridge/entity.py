"""Base entities and device info for the controller and zone devices (SPEC §5.4)."""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from homeassistant.const import MAJOR_VERSION, MINOR_VERSION
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .api import ZoneState
from .const import CONF_ENABLED, CONF_SOURCES, CONF_ZONES, DOMAIN, NUM_SOURCES
from .coordinator import RnetBridgeCoordinator

# HA 2026.8 added DeviceInfo.via_device_id (a device registry id) and deprecated
# via_device (an identifier tuple, removed in 2027.8). The hacs.json minimum (2026.3)
# only has via_device, so use whichever this HA has.
_HAS_VIA_DEVICE_ID = "via_device_id" in DeviceInfo.__annotations__
# DeviceInfo.suggested_area is deprecated from 2026.9 ("use code which ignores
# suggested_area"); SPEC §5.4 asks for it, so only set it where it is still wanted.
_SUGGEST_AREA = (MAJOR_VERSION, MINOR_VERSION) < (2026, 9)


def controller_identifier(bridge_id: str) -> tuple[str, str]:
    """Device registry identifier of the controller device."""
    return (DOMAIN, bridge_id)


def zone_identifier(bridge_id: str, zone: int) -> tuple[str, str]:
    """Device registry identifier of a zone device."""
    return (DOMAIN, f"{bridge_id}_zone{zone}")


def zone_name(options: Mapping[str, Any], zone: int) -> str:
    """The configured zone name (SPEC §5.3)."""
    return str(options[CONF_ZONES][str(zone)].get("name") or f"Zone {zone}")


def zone_enabled(options: Mapping[str, Any], zone: int) -> bool:
    """Whether the zone is enabled in the options."""
    return bool(options[CONF_ZONES][str(zone)].get(CONF_ENABLED, True))


def enabled_sources(options: Mapping[str, Any]) -> list[tuple[int, str]]:
    """(source number, name) for enabled sources, in input order."""
    sources: list[tuple[int, str]] = []
    for n in range(1, NUM_SOURCES + 1):
        item = options[CONF_SOURCES][str(n)]
        if item.get(CONF_ENABLED, True):
            sources.append((n, str(item.get("name") or f"Source {n}")))
    return sources


class RnetEntity(CoordinatorEntity[RnetBridgeCoordinator]):
    """Common base: has_entity_name, available while connected."""

    _attr_has_entity_name = True

    @property
    def available(self) -> bool:
        """WebSocket connected (subclasses add their own conditions)."""
        return super().available and self.coordinator.data.connected


class RnetControllerEntity(RnetEntity):
    """An entity on the controller device."""

    def __init__(self, coordinator: RnetBridgeCoordinator, key: str) -> None:
        """Attach to the controller device; `key` names the entity."""
        super().__init__(coordinator)
        bridge_id = coordinator.bridge_id
        self._attr_unique_id = f"{bridge_id}_{key}"
        self._attr_translation_key = key
        self._attr_device_info = DeviceInfo(
            identifiers={controller_identifier(bridge_id)}
        )


class RnetZoneEntity(RnetEntity):
    """An entity on a zone device.

    Unavailable when the WebSocket is down, the RNET link is down, or the zone
    hasn't been polled yet (valid:false) (SPEC §5.4).
    """

    def __init__(
        self, coordinator: RnetBridgeCoordinator, zone: int, key: str | None
    ) -> None:
        """Attach to zone `zone`'s device; `key` None for the main entity."""
        super().__init__(coordinator)
        self._zone = zone
        bridge_id = coordinator.bridge_id
        self._attr_unique_id = (
            f"{bridge_id}_zone{zone}"
            if key is None
            else f"{bridge_id}_zone{zone}_{key}"
        )
        if key is not None:
            self._attr_translation_key = key
        name = zone_name(coordinator.config_entry.options, zone)
        info = DeviceInfo(
            identifiers={zone_identifier(bridge_id, zone)},
            name=name,
            manufacturer="Russound",
            model="RNET zone",
        )
        if _SUGGEST_AREA:
            info["suggested_area"] = name
        if _HAS_VIA_DEVICE_ID:
            info["via_device_id"] = coordinator.controller_device_id
        else:
            info["via_device"] = controller_identifier(bridge_id)  # type: ignore[typeddict-unknown-key]
        self._attr_device_info = info

    @property
    def zone_state(self) -> ZoneState:
        """This zone's latest state."""
        return self.coordinator.data.zones[self._zone]

    @property
    def available(self) -> bool:
        """Connected, RNET link up, and the zone has been polled."""
        return (
            super().available
            and self.coordinator.data.link.up
            and self.zone_state.valid
        )
