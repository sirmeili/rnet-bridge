"""Entities on the fake bridge: media players, numbers, switch, buttons, sensors."""

from __future__ import annotations

import asyncio
from collections.abc import Callable
from typing import Any
from unittest.mock import patch

from homeassistant.components.media_player import (
    ATTR_INPUT_SOURCE,
    ATTR_INPUT_SOURCE_LIST,
    ATTR_MEDIA_VOLUME_LEVEL,
    ATTR_MEDIA_VOLUME_MUTED,
)
from homeassistant.const import ATTR_ENTITY_ID, STATE_OFF, STATE_ON, STATE_UNAVAILABLE
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError, ServiceValidationError
from homeassistant.helpers import entity_registry as er
import pytest

from custom_components.rnet_bridge.const import CONF_SOURCES, DOMAIN

from .conftest import BRIDGE_ID, FakeBridge, default_options, make_entry


async def _wait_for(predicate: Callable[[], bool], within: float = 3.0) -> None:
    """Poll HA state set by pushes (no event to wait on)."""
    async with asyncio.timeout(within):
        while not predicate():  # noqa: ASYNC110
            await asyncio.sleep(0.02)


def _entity_id(hass: HomeAssistant, platform: str, unique_suffix: str) -> str:
    entity_id = er.async_get(hass).async_get_entity_id(
        platform, DOMAIN, f"{BRIDGE_ID}_{unique_suffix}"
    )
    assert entity_id is not None, f"no {platform} for {unique_suffix}"
    return entity_id


def _state(hass: HomeAssistant, entity_id: str) -> Any:
    state = hass.states.get(entity_id)
    assert state is not None, entity_id
    return state


async def _setup(
    hass: HomeAssistant, bridge: FakeBridge, options: dict[str, Any] | None = None
) -> Any:
    entry = make_entry(bridge.port, options)
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    return entry


async def test_entities_created(hass: HomeAssistant, fake_bridge: FakeBridge) -> None:
    """Six zones' entities plus the controller's; hidden ones are disabled."""
    await _setup(hass, fake_bridge)
    registry = er.async_get(hass)
    for zone in range(1, 7):
        assert _entity_id(hass, "media_player", f"zone{zone}")
        for key in ("bass", "treble", "balance", "turn_on_volume"):
            assert _entity_id(hass, "number", f"zone{zone}_{key}")
        assert _entity_id(hass, "switch", f"zone{zone}_loudness")

    media = _state(hass, _entity_id(hass, "media_player", "zone1"))
    assert media.attributes["friendly_name"] == "Zone 1"  # named after the device
    bass = _state(hass, _entity_id(hass, "number", "zone1_bass"))
    assert bass.attributes["friendly_name"] == "Zone 1 Bass"

    all_on = registry.async_get(_entity_id(hass, "button", "all_on"))
    assert all_on is not None
    assert all_on.disabled_by is er.RegistryEntryDisabler.INTEGRATION
    for key in ("checksum_errors", "timeouts", "duplicate_replies"):
        sensor = registry.async_get(_entity_id(hass, "sensor", key))
        assert sensor is not None
        assert sensor.disabled_by is er.RegistryEntryDisabler.INTEGRATION
    assert (
        _state(hass, _entity_id(hass, "binary_sensor", "rnet_link")).state == STATE_ON
    )
    assert (
        _state(hass, _entity_id(hass, "button", "all_off")).state != STATE_UNAVAILABLE
    )


async def test_media_player_state(hass: HomeAssistant, fake_bridge: FakeBridge) -> None:
    """Power, volume (raw/50), mute, and the source only while on."""
    await _setup(hass, fake_bridge)
    entity_id = _entity_id(hass, "media_player", "zone1")
    assert _state(hass, entity_id).state == STATE_OFF
    assert _state(hass, entity_id).attributes.get(ATTR_INPUT_SOURCE) is None

    await fake_bridge.set_zone(1, power=True, volume=35, source=2)
    await _wait_for(lambda: _state(hass, entity_id).state == STATE_ON)
    attrs = _state(hass, entity_id).attributes
    assert attrs[ATTR_MEDIA_VOLUME_LEVEL] == pytest.approx(0.7)
    assert attrs[ATTR_INPUT_SOURCE] == "Source 2"
    assert attrs[ATTR_INPUT_SOURCE_LIST] == [f"Source {n}" for n in range(1, 7)]
    assert attrs[ATTR_MEDIA_VOLUME_MUTED] is False


async def test_media_player_services(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """Every media player service reaches the bridge and the push updates state."""
    await _setup(hass, fake_bridge)
    entity_id = _entity_id(hass, "media_player", "zone2")
    target = {ATTR_ENTITY_ID: entity_id}

    await hass.services.async_call("media_player", "turn_on", target, blocking=True)
    await _wait_for(lambda: _state(hass, entity_id).state == STATE_ON)

    await hass.services.async_call(
        "media_player",
        "volume_set",
        {**target, ATTR_MEDIA_VOLUME_LEVEL: 0.5},
        blocking=True,
    )
    await _wait_for(lambda: fake_bridge.zones[2].volume == 25)
    await _wait_for(
        lambda: _state(hass, entity_id).attributes[ATTR_MEDIA_VOLUME_LEVEL] == 0.5
    )

    await hass.services.async_call("media_player", "volume_up", target, blocking=True)
    await _wait_for(lambda: fake_bridge.zones[2].volume == 26)
    await hass.services.async_call("media_player", "volume_down", target, blocking=True)
    await _wait_for(lambda: fake_bridge.zones[2].volume == 25)

    await hass.services.async_call(
        "media_player",
        "volume_mute",
        {**target, ATTR_MEDIA_VOLUME_MUTED: True},
        blocking=True,
    )
    await _wait_for(lambda: _state(hass, entity_id).attributes[ATTR_MEDIA_VOLUME_MUTED])
    await hass.services.async_call(
        "media_player",
        "volume_mute",
        {**target, ATTR_MEDIA_VOLUME_MUTED: False},
        blocking=True,
    )
    await _wait_for(lambda: fake_bridge.zones[2].volume == 25)

    await hass.services.async_call(
        "media_player",
        "select_source",
        {**target, ATTR_INPUT_SOURCE: "Source 3"},
        blocking=True,
    )
    await _wait_for(
        lambda: _state(hass, entity_id).attributes.get(ATTR_INPUT_SOURCE) == "Source 3"
    )

    await hass.services.async_call("media_player", "turn_off", target, blocking=True)
    await _wait_for(lambda: _state(hass, entity_id).state == STATE_OFF)
    commands = [c["cmd"] for c in fake_bridge.commands]
    assert commands == [
        "set_power",
        "set_volume",
        "volume_up",
        "volume_down",
        "set_mute",
        "set_mute",
        "set_source",
        "set_power",
    ]


async def test_source_names_and_disabled_sources(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """Configured names; a disabled source picked at a keypad shows as Source N."""
    options = default_options()
    options[CONF_SOURCES]["2"]["name"] = "Sonos"
    options[CONF_SOURCES]["6"]["enabled"] = False
    await _setup(hass, fake_bridge, options)
    entity_id = _entity_id(hass, "media_player", "zone1")

    await fake_bridge.set_zone(1, power=True, source=2)
    await _wait_for(
        lambda: _state(hass, entity_id).attributes.get(ATTR_INPUT_SOURCE) == "Sonos"
    )
    assert _state(hass, entity_id).attributes[ATTR_INPUT_SOURCE_LIST] == [
        "Source 1",
        "Sonos",
        "Source 3",
        "Source 4",
        "Source 5",
    ]
    await fake_bridge.set_zone(1, source=6)
    await _wait_for(
        lambda: _state(hass, entity_id).attributes.get(ATTR_INPUT_SOURCE) == "Source 6"
    )

    with pytest.raises(ServiceValidationError) as err:
        await hass.services.async_call(
            "media_player",
            "select_source",
            {ATTR_ENTITY_ID: entity_id, ATTR_INPUT_SOURCE: "Source 6"},
            blocking=True,
        )
    assert err.value.translation_key == "unknown_source"

    await hass.services.async_call(
        "media_player",
        "select_source",
        {ATTR_ENTITY_ID: entity_id, ATTR_INPUT_SOURCE: "Sonos"},
        blocking=True,
    )
    await _wait_for(lambda: fake_bridge.zones[1].source == 2)


async def test_numbers(hass: HomeAssistant, fake_bridge: FakeBridge) -> None:
    """Tone values pass through; turn-on volume is shown x2 as a percentage."""
    await _setup(hass, fake_bridge)
    bass = _entity_id(hass, "number", "zone3_bass")
    tov = _entity_id(hass, "number", "zone3_turn_on_volume")
    assert float(_state(hass, tov).state) == 40  # fake default raw 20
    assert _state(hass, tov).attributes["unit_of_measurement"] == "%"

    await hass.services.async_call(
        "number", "set_value", {ATTR_ENTITY_ID: bass, "value": -4}, blocking=True
    )
    await _wait_for(lambda: float(_state(hass, bass).state) == -4)
    assert fake_bridge.zones[3].bass == -4

    await hass.services.async_call(
        "number", "set_value", {ATTR_ENTITY_ID: tov, "value": 70}, blocking=True
    )
    await _wait_for(lambda: float(_state(hass, tov).state) == 70)
    assert fake_bridge.zones[3].turn_on_volume == 35

    balance = _entity_id(hass, "number", "zone3_balance")
    await fake_bridge.set_zone(3, balance=-10)
    await _wait_for(lambda: float(_state(hass, balance).state) == -10)


async def test_loudness_switch(hass: HomeAssistant, fake_bridge: FakeBridge) -> None:
    """Loudness on and off."""
    await _setup(hass, fake_bridge)
    entity_id = _entity_id(hass, "switch", "zone4_loudness")
    assert _state(hass, entity_id).state == STATE_OFF
    await hass.services.async_call(
        "switch", "turn_on", {ATTR_ENTITY_ID: entity_id}, blocking=True
    )
    await _wait_for(lambda: _state(hass, entity_id).state == STATE_ON)
    await hass.services.async_call(
        "switch", "turn_off", {ATTR_ENTITY_ID: entity_id}, blocking=True
    )
    await _wait_for(lambda: _state(hass, entity_id).state == STATE_OFF)


async def test_buttons(hass: HomeAssistant, fake_bridge: FakeBridge) -> None:
    """All Zones Off turns everything off; Resync sends refresh for all zones."""
    await _setup(hass, fake_bridge)
    await fake_bridge.set_zone(1, power=True)
    await fake_bridge.set_zone(5, power=True)
    zone5 = _entity_id(hass, "media_player", "zone5")
    await _wait_for(lambda: _state(hass, zone5).state == STATE_ON)

    await hass.services.async_call(
        "button",
        "press",
        {ATTR_ENTITY_ID: _entity_id(hass, "button", "all_off")},
        blocking=True,
    )
    await _wait_for(lambda: _state(hass, zone5).state == STATE_OFF)

    await hass.services.async_call(
        "button",
        "press",
        {ATTR_ENTITY_ID: _entity_id(hass, "button", "resync")},
        blocking=True,
    )
    assert fake_bridge.commands[-1]["cmd"] == "refresh"
    assert "zone" not in fake_bridge.commands[-1]


async def test_link_down(hass: HomeAssistant, fake_bridge: FakeBridge) -> None:
    """Link down: zones and buttons unavailable, the link sensor reads off."""
    await _setup(hass, fake_bridge)
    media = _entity_id(hass, "media_player", "zone1")
    link = _entity_id(hass, "binary_sensor", "rnet_link")
    button = _entity_id(hass, "button", "all_off")

    await fake_bridge.set_link(False)
    await _wait_for(lambda: _state(hass, link).state == STATE_OFF)
    assert _state(hass, media).state == STATE_UNAVAILABLE
    assert _state(hass, button).state == STATE_UNAVAILABLE

    await fake_bridge.set_link(True)
    await _wait_for(lambda: _state(hass, media).state != STATE_UNAVAILABLE)


async def test_zone_not_polled_is_unavailable(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """valid:false (not polled yet) makes that zone's entities unavailable."""
    fake_bridge.zones[6].valid = False
    await _setup(hass, fake_bridge)
    assert _state(hass, _entity_id(hass, "media_player", "zone6")).state == (
        STATE_UNAVAILABLE
    )
    assert _state(hass, _entity_id(hass, "number", "zone6_bass")).state == (
        STATE_UNAVAILABLE
    )
    assert _state(hass, _entity_id(hass, "media_player", "zone5")).state == STATE_OFF


async def test_unavailable_while_disconnected(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """A dropped connection makes everything unavailable until it reconnects."""
    with patch("custom_components.rnet_bridge.coordinator.RECONNECT_MIN_S", 0.3):
        await _setup(hass, fake_bridge)
        media = _entity_id(hass, "media_player", "zone1")
        link = _entity_id(hass, "binary_sensor", "rnet_link")
        await fake_bridge.drop_clients()
        await _wait_for(lambda: _state(hass, media).state == STATE_UNAVAILABLE)
        assert _state(hass, link).state == STATE_UNAVAILABLE
        await _wait_for(lambda: _state(hass, media).state != STATE_UNAVAILABLE)


async def test_command_error_is_raised(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """A bridge error surfaces as a translated HomeAssistantError."""
    await _setup(hass, fake_bridge)
    entity_id = _entity_id(hass, "number", "zone1_bass")
    fake_bridge.link_up = False  # without a push, the entity still looks available
    with pytest.raises(HomeAssistantError) as err:
        await hass.services.async_call(
            "number",
            "set_value",
            {ATTR_ENTITY_ID: entity_id, "value": 1},
            blocking=True,
        )
    assert err.value.translation_key == "command_link_down"
