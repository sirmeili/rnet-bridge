"""RNET link counters on the controller device (SPEC §5.4)."""

from __future__ import annotations

from homeassistant.components.sensor import (
    SensorEntity,
    SensorEntityDescription,
    SensorStateClass,
)
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import RnetConfigEntry
from .coordinator import RnetBridgeCoordinator
from .entity import RnetControllerEntity

PARALLEL_UPDATES = 0

# Keys match the `link` stats in docs/protocol.md. Counters reset when the bridge
# reboots, which total_increasing handles.
SENSORS: tuple[SensorEntityDescription, ...] = tuple(
    SensorEntityDescription(
        key=key,
        translation_key=key,
        state_class=SensorStateClass.TOTAL_INCREASING,
        entity_category=EntityCategory.DIAGNOSTIC,
        entity_registry_enabled_default=False,
    )
    for key in ("checksum_errors", "timeouts", "duplicate_replies")
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: RnetConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Add the link counter sensors."""
    coordinator = entry.runtime_data
    async_add_entities(RnetStatSensor(coordinator, d) for d in SENSORS)


class RnetStatSensor(RnetControllerEntity, SensorEntity):
    """One counter from the bridge's link stats."""

    def __init__(
        self, coordinator: RnetBridgeCoordinator, description: SensorEntityDescription
    ) -> None:
        """Set up `description`."""
        super().__init__(coordinator, description.key)
        self.entity_description = description

    @property
    def native_value(self) -> int | None:
        """The counter, or None if the bridge didn't send it."""
        return self.coordinator.data.link.stats.get(self.entity_description.key)
