"""Diagnostics, send_raw, reauth and the proto repair issue (M7)."""

from __future__ import annotations

import asyncio
from collections.abc import Callable
from unittest.mock import patch

from homeassistant.config_entries import SOURCE_REAUTH, ConfigEntryState
from homeassistant.const import CONF_TOKEN
from homeassistant.core import HomeAssistant
from homeassistant.data_entry_flow import FlowResultType
from homeassistant.exceptions import HomeAssistantError, ServiceValidationError
from homeassistant.helpers import issue_registry as ir
from homeassistant.setup import async_setup_component
import pytest
from pytest_homeassistant_custom_component.components.diagnostics import (
    get_diagnostics_for_config_entry,
)
from pytest_homeassistant_custom_component.typing import ClientSessionGenerator

from custom_components.rnet_bridge.const import DOMAIN

from .conftest import BRIDGE_ID, FakeBridge, make_entry


async def _wait_for(predicate: Callable[[], bool], within: float = 3.0) -> None:
    async with asyncio.timeout(within):
        while not predicate():  # noqa: ASYNC110
            await asyncio.sleep(0.02)


def _reauth_flows(hass: HomeAssistant) -> list[dict]:  # type: ignore[type-arg]
    return [
        f
        for f in hass.config_entries.flow.async_progress()
        if f["context"].get("source") == SOURCE_REAUTH
    ]


# --- diagnostics ---


async def test_diagnostics_redacts_token(
    hass: HomeAssistant,
    hass_client: ClientSessionGenerator,
    fake_bridge: FakeBridge,
) -> None:
    """Diagnostics carry state and stats, never the token."""
    fake_bridge.token = "s3cret"
    entry = make_entry(fake_bridge.port, **{CONF_TOKEN: "s3cret"})
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert await async_setup_component(hass, "diagnostics", {})

    diag = await get_diagnostics_for_config_entry(hass, hass_client, entry)
    assert diag["entry"]["data"][CONF_TOKEN] == "**REDACTED**"
    assert "s3cret" not in str(diag)
    assert diag["hello"]["bridge_id"] == BRIDGE_ID
    assert diag["api_info"]["network"] == "ethernet"
    assert diag["connected"] is True
    assert diag["link"]["up"] is True
    assert "checksum_errors" in diag["link"]["stats"]
    assert sorted(diag["zones"]) == ["1", "2", "3", "4", "5", "6"]
    assert diag["entry"]["options"]["zones"]["1"]["name"] == "Zone 1"


# --- send_raw ---


async def test_send_raw(hass: HomeAssistant, fake_bridge: FakeBridge) -> None:
    """The service sends bytes, appending checksum and F7 on request."""
    entry = make_entry(fake_bridge.port)
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()

    await hass.services.async_call(
        DOMAIN,
        "send_raw",
        {
            "config_entry_id": entry.entry_id,
            "hex": "F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 01 00 00 00 01",
            "fix_checksum": True,
        },
        blocking=True,
    )
    # SPEC §3.10 "Zone 1 On": checksum 12, then F7.
    assert fake_bridge.raw_sent[-1].hex(" ").upper().endswith("00 01 12 F7")

    await hass.services.async_call(
        DOMAIN,
        "send_raw",
        {"config_entry_id": entry.entry_id, "hex": "F0 00 7F"},
        blocking=True,
    )
    assert fake_bridge.raw_sent[-1] == bytes.fromhex("F0007F")

    with pytest.raises(HomeAssistantError) as err:
        await hass.services.async_call(
            DOMAIN,
            "send_raw",
            {"config_entry_id": entry.entry_id, "hex": "ZZ"},
            blocking=True,
        )
    assert err.value.translation_key == "command_invalid"


async def test_send_raw_bad_entry(hass: HomeAssistant, fake_bridge: FakeBridge) -> None:
    """Unknown or unloaded entries are rejected with a clear message."""
    entry = make_entry(fake_bridge.port)
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()

    with pytest.raises(ServiceValidationError) as err:
        await hass.services.async_call(
            DOMAIN,
            "send_raw",
            {"config_entry_id": "nope", "hex": "F0"},
            blocking=True,
        )
    assert err.value.translation_key == "entry_not_found"

    assert await hass.config_entries.async_unload(entry.entry_id)
    with pytest.raises(ServiceValidationError) as err:
        await hass.services.async_call(
            DOMAIN,
            "send_raw",
            {"config_entry_id": entry.entry_id, "hex": "F0"},
            blocking=True,
        )
    assert err.value.translation_key == "entry_not_loaded"


# --- reauth ---


async def test_reauth_at_setup(hass: HomeAssistant, fake_bridge: FakeBridge) -> None:
    """A rejected token starts reauth; the new token is stored and the entry loads."""
    fake_bridge.token = "new-token"
    entry = make_entry(fake_bridge.port, **{CONF_TOKEN: "old-token"})
    entry.add_to_hass(hass)
    await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.SETUP_ERROR

    flows = _reauth_flows(hass)
    assert len(flows) == 1
    assert flows[0]["step_id"] == "reauth_confirm"

    result = await hass.config_entries.flow.async_configure(
        flows[0]["flow_id"], {CONF_TOKEN: "still-wrong"}
    )
    assert result["errors"] == {"base": "invalid_auth"}

    result = await hass.config_entries.flow.async_configure(
        flows[0]["flow_id"], {CONF_TOKEN: "new-token"}
    )
    assert result["type"] is FlowResultType.ABORT
    assert result["reason"] == "reauth_successful"
    await hass.async_block_till_done()
    assert entry.data[CONF_TOKEN] == "new-token"
    assert entry.state is ConfigEntryState.LOADED


async def test_reauth_wrong_bridge(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """Reauth refuses to bind the entry to a different bridge."""
    fake_bridge.token = "t"
    entry = make_entry(fake_bridge.port, **{CONF_TOKEN: "x"})
    entry.add_to_hass(hass)
    await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    flow = _reauth_flows(hass)[0]
    fake_bridge.bridge_id = "ffffffffffff"
    result = await hass.config_entries.flow.async_configure(
        flow["flow_id"], {CONF_TOKEN: "t"}
    )
    assert result["type"] is FlowResultType.ABORT
    assert result["reason"] == "wrong_bridge"


async def test_reauth_on_reconnect(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """A token set on the bridge while connected triggers reauth on reconnect."""
    entry = make_entry(fake_bridge.port)
    entry.add_to_hass(hass)
    with patch("custom_components.rnet_bridge.coordinator.RECONNECT_MIN_S", 0.05):
        assert await hass.config_entries.async_setup(entry.entry_id)
        fake_bridge.token = "s3cret"
        await fake_bridge.drop_clients()
        await _wait_for(lambda: len(_reauth_flows(hass)) == 1)


# --- proto repair issue ---


async def test_proto_issue_raised_and_cleared(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """A proto mismatch at setup raises a repair issue; success clears it."""
    fake_bridge.proto = 2
    entry = make_entry(fake_bridge.port)
    entry.add_to_hass(hass)
    await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.SETUP_ERROR

    registry = ir.async_get(hass)
    issue = registry.async_get_issue(DOMAIN, f"unsupported_proto_{entry.entry_id}")
    assert issue is not None
    assert issue.severity is ir.IssueSeverity.ERROR
    assert issue.translation_placeholders == {
        "title": entry.title,
        "proto": "2",
        "supported": "1",
    }

    fake_bridge.proto = 1
    assert await hass.config_entries.async_reload(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.LOADED
    assert (
        registry.async_get_issue(DOMAIN, f"unsupported_proto_{entry.entry_id}") is None
    )


async def test_proto_issue_on_reconnect(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """Firmware updated to another proto while running: issue on reconnect."""
    entry = make_entry(fake_bridge.port)
    entry.add_to_hass(hass)
    with patch("custom_components.rnet_bridge.coordinator.RECONNECT_MIN_S", 0.05):
        assert await hass.config_entries.async_setup(entry.entry_id)
        fake_bridge.proto = 2
        await fake_bridge.drop_clients()
        registry = ir.async_get(hass)
        issue_id = f"unsupported_proto_{entry.entry_id}"
        await _wait_for(lambda: registry.async_get_issue(DOMAIN, issue_id) is not None)
