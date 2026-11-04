"""RNET link connectivity on the controller device (SPEC §5.4)."""

from __future__ import annotations

from homeassistant.components.binary_sensor import (
    BinarySensorDeviceClass,
    BinarySensorEntity,
)
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import RnetConfigEntry
from .coordinator import RnetBridgeCoordinator
from .entity import RnetControllerEntity

PARALLEL_UPDATES = 0


async def async_setup_entry(
    hass: HomeAssistant,
    entry: RnetConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Add the RNET link sensor."""
    async_add_entities([RnetLinkSensor(entry.runtime_data)])


class RnetLinkSensor(RnetControllerEntity, BinarySensorEntity):
    """On while the bridge talks to the controller over RS-232.

    Available whenever the bridge is connected, so a down link reads "off"
    rather than "unavailable".
    """

    _attr_device_class = BinarySensorDeviceClass.CONNECTIVITY
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, coordinator: RnetBridgeCoordinator) -> None:
        """Set up the link sensor."""
        super().__init__(coordinator, "rnet_link")

    @property
    def is_on(self) -> bool:
        """RNET link state."""
        return self.coordinator.data.link.up
