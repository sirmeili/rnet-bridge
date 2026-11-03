"""Options flow: zones and sources can change at any time (SPEC §5.3)."""

from __future__ import annotations

import asyncio
from collections.abc import Callable
from typing import Any

from homeassistant.components.media_player import ATTR_INPUT_SOURCE_LIST
from homeassistant.config_entries import ConfigEntryState
from homeassistant.core import HomeAssistant
from homeassistant.data_entry_flow import FlowResultType
from homeassistant.helpers import device_registry as dr, entity_registry as er

from custom_components.rnet_bridge.const import (
    CONF_ENABLED,
    CONF_SOURCES,
    CONF_ZONES,
    DOMAIN,
)

from .conftest import BRIDGE_ID, FakeBridge, make_entry


async def _wait_for(predicate: Callable[[], bool], within: float = 3.0) -> None:
    async with asyncio.timeout(within):
        while not predicate():  # noqa: ASYNC110
            await asyncio.sleep(0.02)


def _form(names: dict[int, str], disabled: set[int], label: str) -> dict[str, Any]:
    data: dict[str, Any] = {}
    for n in range(1, 7):
        data[f"{n}_enabled"] = n not in disabled
        data[f"{n}_name"] = names.get(n, f"{label} {n}")
    return data


def _zone_device_ids(hass: HomeAssistant, entry_id: str) -> set[str]:
    devices = dr.async_entries_for_config_entry(dr.async_get(hass), entry_id)
    return {next(iter(d.identifiers))[1] for d in devices}


async def test_options_flow_changes_zones_and_sources(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """Rename and disable: the entry reloads, devices and entities follow."""
    entry = make_entry(fake_bridge.port)
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert f"{BRIDGE_ID}_zone5" in _zone_device_ids(hass, entry.entry_id)

    result = await hass.config_entries.options.async_init(entry.entry_id)
    assert result["type"] is FlowResultType.FORM
    assert result["step_id"] == "zones"
    result = await hass.config_entries.options.async_configure(
        result["flow_id"], _form({1: "Kitchen"}, {5}, "Zone")
    )
    assert result["step_id"] == "sources"
    result = await hass.config_entries.options.async_configure(
        result["flow_id"], _form({2: "Sonos"}, {6}, "Source")
    )
    assert result["type"] is FlowResultType.CREATE_ENTRY
    await hass.async_block_till_done()

    assert entry.state is ConfigEntryState.LOADED
    assert entry.options[CONF_ZONES]["5"][CONF_ENABLED] is False
    assert entry.options[CONF_ZONES]["1"]["name"] == "Kitchen"
    assert entry.options[CONF_SOURCES]["2"]["name"] == "Sonos"

    # Zone 5's device and its entities are gone; the others remain.
    ids = _zone_device_ids(hass, entry.entry_id)
    assert f"{BRIDGE_ID}_zone5" not in ids
    assert f"{BRIDGE_ID}_zone4" in ids
    registry = er.async_get(hass)
    assert (
        registry.async_get_entity_id("media_player", DOMAIN, f"{BRIDGE_ID}_zone5")
        is None
    )

    # Zone 1's device is renamed; its entity id stays put.
    devices = dr.async_entries_for_config_entry(dr.async_get(hass), entry.entry_id)
    zone1 = next(d for d in devices if (DOMAIN, f"{BRIDGE_ID}_zone1") in d.identifiers)
    assert zone1.name == "Kitchen"
    zone1_entity = registry.async_get_entity_id(
        "media_player", DOMAIN, f"{BRIDGE_ID}_zone1"
    )
    assert zone1_entity is not None
    await _wait_for(lambda: hass.states.get(zone1_entity) is not None)
    state = hass.states.get(zone1_entity)
    assert state is not None
    assert state.attributes[ATTR_INPUT_SOURCE_LIST] == [
        "Source 1",
        "Sonos",
        "Source 3",
        "Source 4",
        "Source 5",
    ]


async def test_options_flow_prefills_current_values(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """The form shows the stored names and enablement."""
    entry = make_entry(fake_bridge.port)
    entry.add_to_hass(hass)
    hass.config_entries.async_update_entry(
        entry,
        options={
            **entry.options,
            CONF_ZONES: {
                **entry.options[CONF_ZONES],
                "3": {CONF_ENABLED: False, "name": "Patio"},
            },
        },
    )
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    result = await hass.config_entries.options.async_init(entry.entry_id)
    schema = result["data_schema"]
    assert schema is not None
    suggested = {
        str(key): key.description.get("suggested_value")
        for key in schema.schema
        if key.description
    }
    assert suggested["3_enabled"] is False
    assert suggested["3_name"] == "Patio"
