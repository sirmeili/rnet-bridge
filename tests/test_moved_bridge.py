"""The bridge at a new address: mDNS lookup at setup and while reconnecting, the
reconfigure step, and the lookup itself (docs/rnet-notes.md, M8 R6)."""

from __future__ import annotations

import asyncio
from collections.abc import Callable
from typing import Any
from unittest.mock import AsyncMock, MagicMock, patch

from homeassistant.config_entries import ConfigEntryState
from homeassistant.const import CONF_HOST, CONF_PORT
from homeassistant.core import HomeAssistant
from homeassistant.data_entry_flow import FlowResultType
from zeroconf import ServiceStateChange

from custom_components.rnet_bridge.discovery import SERVICE_TYPE, async_find_bridge

from .conftest import BRIDGE_ID, FakeBridge, make_entry

DEAD_PORT = 1  # nothing listens here: connection refused at once


async def _wait_for(predicate: Callable[[], bool], within: float = 5.0) -> None:
    async with asyncio.timeout(within):
        while not predicate():  # noqa: ASYNC110
            await asyncio.sleep(0.02)


async def test_setup_finds_moved_bridge(
    hass: HomeAssistant, fake_bridge: FakeBridge, mock_zeroconf_and_lookup: AsyncMock
) -> None:
    """Stored address dead: the bridge announced elsewhere is used and saved."""
    mock_zeroconf_and_lookup.return_value = [
        ("127.0.0.1", DEAD_PORT),  # a stale address is skipped
        ("127.0.0.1", fake_bridge.port),
    ]
    entry = make_entry(DEAD_PORT)
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.LOADED
    assert entry.data[CONF_PORT] == fake_bridge.port
    mock_zeroconf_and_lookup.assert_awaited_once_with(hass, BRIDGE_ID)


async def test_setup_not_ready_when_not_found(
    hass: HomeAssistant, socket_enabled: None, mock_zeroconf_and_lookup: AsyncMock
) -> None:
    """Nothing announced: retry later, address unchanged."""
    entry = make_entry(DEAD_PORT)
    entry.add_to_hass(hass)
    await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.SETUP_RETRY
    assert entry.data[CONF_PORT] == DEAD_PORT
    mock_zeroconf_and_lookup.assert_awaited_once()


async def test_reconnect_finds_moved_bridge(
    hass: HomeAssistant, fake_bridge: FakeBridge, mock_zeroconf_and_lookup: AsyncMock
) -> None:
    """Connected, then the bridge moves: after failed reconnects the lookup finds
    it, the address is saved, and the entry reloads onto it."""
    entry = make_entry(fake_bridge.port)
    entry.add_to_hass(hass)
    moved = FakeBridge(bridge_id=BRIDGE_ID)
    await moved.start()
    try:
        with patch("custom_components.rnet_bridge.coordinator.RECONNECT_MIN_S", 0.02):
            assert await hass.config_entries.async_setup(entry.entry_id)
            mock_zeroconf_and_lookup.return_value = [("127.0.0.1", moved.port)]
            await fake_bridge.stop()
            await _wait_for(lambda: entry.data[CONF_PORT] == moved.port)
            await hass.async_block_till_done()
        assert entry.state is ConfigEntryState.LOADED
        assert entry.runtime_data.client.port == moved.port
        assert entry.runtime_data.data.connected
    finally:
        await moved.stop()


async def test_reconnect_lookup_ignores_unanswering_address(
    hass: HomeAssistant, fake_bridge: FakeBridge, mock_zeroconf_and_lookup: AsyncMock
) -> None:
    """An announced address that doesn't answer is not saved."""
    entry = make_entry(fake_bridge.port)
    entry.add_to_hass(hass)
    with patch("custom_components.rnet_bridge.coordinator.RECONNECT_MIN_S", 0.02):
        assert await hass.config_entries.async_setup(entry.entry_id)
        mock_zeroconf_and_lookup.return_value = [("127.0.0.1", DEAD_PORT)]
        port = fake_bridge.port
        await fake_bridge.stop()
        await _wait_for(lambda: mock_zeroconf_and_lookup.await_count >= 1)
    assert entry.data[CONF_PORT] == port
    await hass.config_entries.async_unload(entry.entry_id)


async def _start_reconfigure(hass: HomeAssistant, port: int) -> tuple[Any, Any]:
    entry = make_entry(port)
    entry.add_to_hass(hass)
    result = await entry.start_reconfigure_flow(hass)
    assert result["type"] is FlowResultType.FORM
    assert result["step_id"] == "reconfigure"
    return entry, result


async def test_reconfigure_updates_address(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """A new address that answers as the same bridge is saved."""
    entry, result = await _start_reconfigure(hass, DEAD_PORT)
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], {CONF_HOST: "127.0.0.1", CONF_PORT: fake_bridge.port}
    )
    assert result["type"] is FlowResultType.ABORT
    assert result["reason"] == "reconfigure_successful"
    assert entry.data[CONF_PORT] == fake_bridge.port
    assert entry.options  # zones and sources kept


async def test_reconfigure_cannot_connect(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """No answer: the form is shown again with an error."""
    _, result = await _start_reconfigure(hass, fake_bridge.port)
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], {CONF_HOST: "127.0.0.1", CONF_PORT: DEAD_PORT}
    )
    assert result["type"] is FlowResultType.FORM
    assert result["errors"] == {"base": "cannot_connect"}


async def test_reconfigure_wrong_bridge(
    hass: HomeAssistant, socket_enabled: None
) -> None:
    """An address answering as another bridge is refused."""
    other = FakeBridge(bridge_id="ffffffffffff")
    await other.start()
    try:
        entry, result = await _start_reconfigure(hass, DEAD_PORT)
        result = await hass.config_entries.flow.async_configure(
            result["flow_id"], {CONF_HOST: "127.0.0.1", CONF_PORT: other.port}
        )
    finally:
        await other.stop()
    assert result["type"] is FlowResultType.ABORT
    assert result["reason"] == "wrong_bridge"
    assert entry.data[CONF_PORT] == DEAD_PORT


async def test_find_bridge_matches_id(hass: HomeAssistant) -> None:
    """The lookup keeps only the bridge with the wanted id, all its addresses."""
    announced = {
        f"rnet-bridge-e5f6.{SERVICE_TYPE}": (BRIDGE_ID, ["10.0.0.165", "10.0.0.151"]),
        f"rnet-bridge-ffff.{SERVICE_TYPE}": ("ffffffffffff", ["10.0.0.99"]),
    }

    class FakeBrowser:
        def __init__(self, zc: Any, type_: str, handlers: list[Any]) -> None:
            for name in announced:
                handlers[0](
                    zeroconf=zc,
                    service_type=type_,
                    name=name,
                    state_change=ServiceStateChange.Added,
                )

        async def async_cancel(self) -> None:
            pass

    class FakeInfo:
        def __init__(self, type_: str, name: str) -> None:
            self._id, self._addrs = announced[name]
            self.port = 80
            self.properties = {b"id": self._id.encode()}

        async def async_request(self, zc: Any, timeout_ms: int) -> bool:
            return True

        def parsed_addresses(self, version: Any) -> list[str]:
            return self._addrs

    with (
        patch(
            "custom_components.rnet_bridge.discovery.ha_zeroconf.async_get_async_instance",
            AsyncMock(return_value=MagicMock()),
        ),
        patch(
            "custom_components.rnet_bridge.discovery.AsyncServiceBrowser", FakeBrowser
        ),
        patch("custom_components.rnet_bridge.discovery.AsyncServiceInfo", FakeInfo),
        patch("custom_components.rnet_bridge.discovery.BROWSE_S", 0),
    ):
        found = await async_find_bridge(hass, BRIDGE_ID)
    assert sorted(found) == [("10.0.0.151", 80), ("10.0.0.165", 80)]
