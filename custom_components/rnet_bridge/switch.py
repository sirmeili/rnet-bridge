"""Loudness per zone (SPEC §5.4)."""

from __future__ import annotations

from typing import Any

from homeassistant.components.switch import SwitchEntity
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import RnetConfigEntry
from .const import NUM_ZONES
from .coordinator import RnetBridgeCoordinator
from .entity import RnetZoneEntity, zone_enabled

PARALLEL_UPDATES = 0


async def async_setup_entry(
    hass: HomeAssistant,
    entry: RnetConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Add a loudness switch for each enabled zone."""
    coordinator = entry.runtime_data
    async_add_entities(
        RnetLoudnessSwitch(coordinator, zone)
        for zone in range(1, NUM_ZONES + 1)
        if zone_enabled(entry.options, zone)
    )


class RnetLoudnessSwitch(RnetZoneEntity, SwitchEntity):
    """Loudness on/off."""

    _attr_entity_category = EntityCategory.CONFIG

    def __init__(self, coordinator: RnetBridgeCoordinator, zone: int) -> None:
        """Set up zone `zone`."""
        super().__init__(coordinator, zone, "loudness")

    @property
    def is_on(self) -> bool:
        """Loudness state from the zone."""
        return self.zone_state.loudness

    async def async_turn_on(self, **kwargs: Any) -> None:
        """Turn loudness on."""
        await self.coordinator.async_send("set_loudness", self._zone, True)

    async def async_turn_off(self, **kwargs: Any) -> None:
        """Turn loudness off."""
        await self.coordinator.async_send("set_loudness", self._zone, False)
