"""All Zones On/Off and Resync on the controller device (SPEC §5.4)."""

from __future__ import annotations

from dataclasses import dataclass

from homeassistant.components.button import ButtonEntity, ButtonEntityDescription
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import RnetConfigEntry
from .coordinator import RnetBridgeCoordinator
from .entity import RnetControllerEntity

PARALLEL_UPDATES = 0


@dataclass(frozen=True, kw_only=True)
class RnetButtonDescription(ButtonEntityDescription):
    """A button and the bridge command it sends."""

    command: str


BUTTONS: tuple[RnetButtonDescription, ...] = (
    # Does nothing on the CAA6.6 tested (SPEC §11 item 9): hidden until enabled.
    RnetButtonDescription(
        key="all_on",
        translation_key="all_on",
        command="all_on",
        entity_registry_enabled_default=False,
    ),
    RnetButtonDescription(key="all_off", translation_key="all_off", command="all_off"),
    RnetButtonDescription(key="resync", translation_key="resync", command="refresh"),
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: RnetConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Add the controller buttons."""
    coordinator = entry.runtime_data
    async_add_entities(RnetButton(coordinator, d) for d in BUTTONS)


class RnetButton(RnetControllerEntity, ButtonEntity):
    """A controller-wide command."""

    entity_description: RnetButtonDescription

    def __init__(
        self, coordinator: RnetBridgeCoordinator, description: RnetButtonDescription
    ) -> None:
        """Set up `description`."""
        super().__init__(coordinator, description.key)
        self.entity_description = description

    @property
    def available(self) -> bool:
        """Connected and RNET link up."""
        return super().available and self.coordinator.data.link.up

    async def async_press(self) -> None:
        """Send the command (refresh with no zone polls every zone)."""
        await self.coordinator.async_send(self.entity_description.command)
