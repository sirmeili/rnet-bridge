"""Bass, treble, balance and turn-on volume per zone (SPEC §5.4)."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass

from homeassistant.components.number import (
    NumberEntity,
    NumberEntityDescription,
    NumberMode,
)
from homeassistant.const import PERCENTAGE, EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import RnetConfigEntry
from .api import ZoneState
from .const import MAX_TONE, MAX_VOLUME, MIN_TONE, NUM_ZONES
from .coordinator import RnetBridgeCoordinator
from .entity import RnetZoneEntity, zone_enabled

PARALLEL_UPDATES = 0


@dataclass(frozen=True, kw_only=True)
class RnetNumberDescription(NumberEntityDescription):
    """How to read a value from the zone state and which command sets it."""

    value_fn: Callable[[ZoneState], int | None]
    command: str
    # API value -> displayed value (and back with to_api).
    scale: int = 1


NUMBERS: tuple[RnetNumberDescription, ...] = (
    RnetNumberDescription(
        key="bass",
        translation_key="bass",
        native_min_value=MIN_TONE,
        native_max_value=MAX_TONE,
        native_step=1,
        mode=NumberMode.SLIDER,
        entity_category=EntityCategory.CONFIG,
        value_fn=lambda z: z.bass,
        command="set_bass",
    ),
    RnetNumberDescription(
        key="treble",
        translation_key="treble",
        native_min_value=MIN_TONE,
        native_max_value=MAX_TONE,
        native_step=1,
        mode=NumberMode.SLIDER,
        entity_category=EntityCategory.CONFIG,
        value_fn=lambda z: z.treble,
        command="set_treble",
    ),
    RnetNumberDescription(
        key="balance",
        translation_key="balance",
        native_min_value=MIN_TONE,
        native_max_value=MAX_TONE,
        native_step=1,
        mode=NumberMode.SLIDER,
        entity_category=EntityCategory.CONFIG,
        value_fn=lambda z: z.balance,
        command="set_balance",
    ),
    RnetNumberDescription(
        key="turn_on_volume",
        translation_key="turn_on_volume",
        native_min_value=0,
        native_max_value=MAX_VOLUME * 2,
        native_step=2,
        native_unit_of_measurement=PERCENTAGE,
        mode=NumberMode.SLIDER,
        entity_category=EntityCategory.CONFIG,
        value_fn=lambda z: z.turn_on_volume,
        command="set_turn_on_volume",
        scale=2,  # raw 0..50 shown as 0..100 %, like the keypad
    ),
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: RnetConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Add the numbers for each enabled zone."""
    coordinator = entry.runtime_data
    async_add_entities(
        RnetZoneNumber(coordinator, zone, description)
        for zone in range(1, NUM_ZONES + 1)
        if zone_enabled(entry.options, zone)
        for description in NUMBERS
    )


class RnetZoneNumber(RnetZoneEntity, NumberEntity):
    """A zone parameter set through Set Data on the bridge."""

    entity_description: RnetNumberDescription

    def __init__(
        self,
        coordinator: RnetBridgeCoordinator,
        zone: int,
        description: RnetNumberDescription,
    ) -> None:
        """Set up `description` for zone `zone`."""
        super().__init__(coordinator, zone, description.key)
        self.entity_description = description

    @property
    def native_value(self) -> int | None:
        """The current value (None until read, e.g. turn-on volume)."""
        value = self.entity_description.value_fn(self.zone_state)
        return None if value is None else value * self.entity_description.scale

    async def async_set_native_value(self, value: float) -> None:
        """Send the new value; the confirming zone_state updates the entity."""
        api_value = round(value / self.entity_description.scale)
        await self.coordinator.async_send(
            self.entity_description.command, self._zone, api_value
        )
