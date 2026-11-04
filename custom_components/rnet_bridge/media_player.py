"""One media player per enabled zone (SPEC §5.4)."""

from __future__ import annotations

from homeassistant.components.media_player import (
    MediaPlayerDeviceClass,
    MediaPlayerEntity,
    MediaPlayerEntityFeature,
    MediaPlayerState,
)
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import ServiceValidationError
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import RnetConfigEntry
from .const import DOMAIN, MAX_VOLUME, NUM_ZONES
from .coordinator import RnetBridgeCoordinator
from .entity import RnetZoneEntity, enabled_sources, zone_enabled

PARALLEL_UPDATES = 0  # commands queue on the bridge; it answers `busy` when full


async def async_setup_entry(
    hass: HomeAssistant,
    entry: RnetConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Add a media player for each enabled zone."""
    coordinator = entry.runtime_data
    async_add_entities(
        RnetZoneMediaPlayer(coordinator, zone)
        for zone in range(1, NUM_ZONES + 1)
        if zone_enabled(entry.options, zone)
    )


class RnetZoneMediaPlayer(RnetZoneEntity, MediaPlayerEntity):
    """A zone: power, volume, emulated mute and source.

    No optimistic state (CLAUDE.md rule 6): every change shows when the bridge
    pushes the confirming zone_state, about 150 ms after the command.
    """

    _attr_name = None  # named after the zone device
    _attr_device_class = MediaPlayerDeviceClass.SPEAKER
    _attr_supported_features = (
        MediaPlayerEntityFeature.TURN_ON
        | MediaPlayerEntityFeature.TURN_OFF
        | MediaPlayerEntityFeature.VOLUME_SET
        | MediaPlayerEntityFeature.VOLUME_STEP
        | MediaPlayerEntityFeature.VOLUME_MUTE
        | MediaPlayerEntityFeature.SELECT_SOURCE
    )
    _attr_volume_step = 1 / MAX_VOLUME  # one raw step = 2 on the keypad

    def __init__(self, coordinator: RnetBridgeCoordinator, zone: int) -> None:
        """Set up zone `zone`."""
        super().__init__(coordinator, zone, None)
        self._sources = enabled_sources(coordinator.config_entry.options)

    @property
    def state(self) -> MediaPlayerState:
        """On or off."""
        return MediaPlayerState.ON if self.zone_state.power else MediaPlayerState.OFF

    @property
    def volume_level(self) -> float:
        """0..1 from raw 0..50. For a zone that is off the CAA6.6 reports its
        turn-on volume here (SPEC §3.4)."""
        return self.zone_state.volume / MAX_VOLUME

    @property
    def is_volume_muted(self) -> bool:
        """Emulated mute (SPEC §4.3)."""
        return self.zone_state.muted

    @property
    def source_list(self) -> list[str]:
        """Enabled source names, in input order."""
        return [name for _, name in self._sources]

    @property
    def source(self) -> str | None:
        """The selected source; None while off, when the value isn't meaningful.

        A source that isn't enabled (picked at a keypad) shows as "Source N".
        """
        state = self.zone_state
        if not state.power:
            return None
        for number, name in self._sources:
            if number == state.source:
                return name
        return f"Source {state.source}"

    async def async_turn_on(self) -> None:
        """Turn the zone on."""
        await self.coordinator.async_send("set_power", self._zone, True)

    async def async_turn_off(self) -> None:
        """Turn the zone off."""
        await self.coordinator.async_send("set_power", self._zone, False)

    async def async_set_volume_level(self, volume: float) -> None:
        """Set the volume (0..1 -> raw 0..50)."""
        raw = max(0, min(MAX_VOLUME, round(volume * MAX_VOLUME)))
        await self.coordinator.async_send("set_volume", self._zone, raw)

    async def async_volume_up(self) -> None:
        """One raw step up (the bridge steps from its cached volume)."""
        await self.coordinator.async_send("volume_up", self._zone)

    async def async_volume_down(self) -> None:
        """One raw step down."""
        await self.coordinator.async_send("volume_down", self._zone)

    async def async_mute_volume(self, mute: bool) -> None:
        """Mute or unmute (emulated by the bridge)."""
        await self.coordinator.async_send("set_mute", self._zone, mute)

    async def async_select_source(self, source: str) -> None:
        """Select a source by its configured name."""
        for number, name in self._sources:
            if name == source:
                await self.coordinator.async_send("set_source", self._zone, number)
                return
        raise ServiceValidationError(
            translation_domain=DOMAIN,
            translation_key="unknown_source",
            translation_placeholders={"source": source},
        )
