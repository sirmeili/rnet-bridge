"""Setup, unload, the controller device, pushes, reconnect and command errors."""

from __future__ import annotations

import asyncio
from collections.abc import Callable
from unittest.mock import patch

from homeassistant.config_entries import ConfigEntryState
from homeassistant.const import CONF_TOKEN
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import device_registry as dr
import pytest

from custom_components.rnet_bridge.const import DOMAIN

from .conftest import BRIDGE_ID, FakeBridge, make_entry


async def _wait_for(predicate: Callable[[], bool], within: float = 3.0) -> None:
    """Poll coordinator state set by pushes (no event to wait on)."""
    async with asyncio.timeout(within):
        while not predicate():  # noqa: ASYNC110
            await asyncio.sleep(0.02)


async def test_setup_and_unload(hass: HomeAssistant, fake_bridge: FakeBridge) -> None:
    """The entry loads, registers the controller device, and unloads cleanly."""
    entry = make_entry(fake_bridge.port)
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.LOADED

    # Works on the hacs.json minimum (2026.3) and current releases alike.
    devices = dr.async_entries_for_config_entry(dr.async_get(hass), entry.entry_id)
    assert len(devices) == 1
    device = devices[0]
    assert device.identifiers == {(DOMAIN, BRIDGE_ID)}
    assert device.name == "Russound Controller"
    assert device.model == "RNET Bridge"
    assert device.sw_version == "0.0.0-fake"
    assert device.configuration_url == f"http://127.0.0.1:{fake_bridge.port}/"

    coordinator = entry.runtime_data
    data = coordinator.data
    assert data.connected
    assert data.available
    assert sorted(data.zones) == [1, 2, 3, 4, 5, 6]

    assert await hass.config_entries.async_unload(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.NOT_LOADED
    assert not coordinator.client.connected


async def test_setup_not_ready(hass: HomeAssistant, fake_bridge: FakeBridge) -> None:
    """Bridge unreachable at startup: retry later."""
    port = fake_bridge.port
    await fake_bridge.stop()
    entry = make_entry(port)
    entry.add_to_hass(hass)
    await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.SETUP_RETRY


@pytest.mark.parametrize(
    ("token", "proto"),
    [(None, 1), ("wrong", 1), (None, 2)],
    ids=["none", "wrong", "proto"],
)
async def test_setup_error(
    hass: HomeAssistant, fake_bridge: FakeBridge, token: str | None, proto: int
) -> None:
    """Bad token or another proto: a setup error the user must fix."""
    fake_bridge.proto = proto
    if proto == 1:
        fake_bridge.token = "s3cret"
    entry = make_entry(fake_bridge.port, **({CONF_TOKEN: token} if token else {}))
    entry.add_to_hass(hass)
    await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.SETUP_ERROR


async def test_setup_with_token(hass: HomeAssistant, fake_bridge: FakeBridge) -> None:
    """The stored token is sent."""
    fake_bridge.token = "s3cret"
    entry = make_entry(fake_bridge.port, **{CONF_TOKEN: "s3cret"})
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.LOADED


async def test_pushes_update_coordinator(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """Keypad changes and link changes reach the coordinator."""
    entry = make_entry(fake_bridge.port)
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    coordinator = entry.runtime_data

    await fake_bridge.set_zone(4, power=True, volume=33)
    await _wait_for(lambda: coordinator.data.zones[4].volume == 33)
    assert coordinator.data.zones[4].power

    await fake_bridge.set_link(False)
    await _wait_for(lambda: not coordinator.data.link.up)
    assert coordinator.data.connected
    assert not coordinator.data.available


async def test_reconnects_after_drop(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """A dropped connection marks the bridge unavailable, then reconnects."""
    entry = make_entry(fake_bridge.port)
    entry.add_to_hass(hass)
    with patch("custom_components.rnet_bridge.coordinator.RECONNECT_MIN_S", 0.05):
        assert await hass.config_entries.async_setup(entry.entry_id)
        coordinator = entry.runtime_data
        await fake_bridge.drop_clients()
        await _wait_for(lambda: not coordinator.data.connected)
        await _wait_for(lambda: coordinator.data.connected)
    # State changed while away is picked up from the new snapshot.
    await fake_bridge.set_zone(2, volume=11)
    await _wait_for(lambda: coordinator.data.zones[2].volume == 11)


async def test_send_command(hass: HomeAssistant, fake_bridge: FakeBridge) -> None:
    """Commands go through; errors become translated HomeAssistantErrors."""
    entry = make_entry(fake_bridge.port)
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    coordinator = entry.runtime_data

    await coordinator.async_send("set_volume", 1, 25)
    await _wait_for(lambda: coordinator.data.zones[1].volume == 25)

    with pytest.raises(HomeAssistantError) as err:
        await coordinator.async_send("set_volume", 1, 99)
    assert err.value.translation_key == "command_invalid"

    await fake_bridge.set_link(False)
    with pytest.raises(HomeAssistantError) as err:
        await coordinator.async_send("set_power", 1, True)
    assert err.value.translation_key == "command_link_down"

    await coordinator.client.close()
    with pytest.raises(HomeAssistantError) as err:
        await coordinator.async_send("refresh")
    assert err.value.translation_key == "not_connected"
