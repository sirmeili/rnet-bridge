"""Config flow: manual and zeroconf, errors, zones and sources (SPEC §5.3)."""

from __future__ import annotations

from ipaddress import ip_address
from typing import Any

from homeassistant.config_entries import SOURCE_USER, SOURCE_ZEROCONF
from homeassistant.const import CONF_HOST, CONF_PORT, CONF_TOKEN
from homeassistant.core import HomeAssistant
from homeassistant.data_entry_flow import FlowResultType
from homeassistant.helpers.service_info.zeroconf import ZeroconfServiceInfo
import pytest

from custom_components.rnet_bridge.const import (
    CONF_ENABLED,
    CONF_SOURCES,
    CONF_ZONES,
    DOMAIN,
)

from .conftest import BRIDGE_ID, FakeBridge, make_entry


def _zones_input(**overrides: Any) -> dict[str, Any]:
    data: dict[str, Any] = {}
    for n in range(1, 7):
        data[f"{n}_enabled"] = True
        data[f"{n}_name"] = f"Zone {n}"
    data.update(overrides)
    return data


def _sources_input(**overrides: Any) -> dict[str, Any]:
    data: dict[str, Any] = {}
    for n in range(1, 7):
        data[f"{n}_enabled"] = True
        data[f"{n}_name"] = f"Source {n}"
    data.update(overrides)
    return data


def _discovery(port: int, **props: str) -> ZeroconfServiceInfo:
    properties = {"id": BRIDGE_ID, "fw": "0.0.3", "proto": "1", **props}
    return ZeroconfServiceInfo(
        ip_address=ip_address("127.0.0.1"),
        ip_addresses=[ip_address("127.0.0.1")],
        hostname="rnet-bridge-e5f6.local.",
        name="rnet-bridge-e5f6._rnetbridge._tcp.local.",
        port=port,
        type="_rnetbridge._tcp.local.",
        properties=properties,
    )


async def test_user_flow(hass: HomeAssistant, fake_bridge: FakeBridge) -> None:
    """Host -> zones -> sources -> entry, with names and enablement stored."""
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": SOURCE_USER}
    )
    assert result["type"] is FlowResultType.FORM
    assert result["step_id"] == "user"

    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], {CONF_HOST: "127.0.0.1", CONF_PORT: fake_bridge.port}
    )
    assert result["type"] is FlowResultType.FORM
    assert result["step_id"] == "zones"

    result = await hass.config_entries.flow.async_configure(
        result["flow_id"],
        _zones_input(**{"1_name": "Kitchen", "5_enabled": False, "6_name": "  "}),
    )
    assert result["step_id"] == "sources"

    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], _sources_input(**{"2_name": "Sonos", "6_enabled": False})
    )
    assert result["type"] is FlowResultType.CREATE_ENTRY
    assert result["title"] == "RNET Bridge e5f6"
    assert result["data"] == {CONF_HOST: "127.0.0.1", CONF_PORT: fake_bridge.port}
    zones = result["options"][CONF_ZONES]
    assert zones["1"] == {CONF_ENABLED: True, "name": "Kitchen"}
    assert zones["5"] == {CONF_ENABLED: False, "name": "Zone 5"}
    assert zones["6"]["name"] == "Zone 6"  # blank name falls back
    sources = result["options"][CONF_SOURCES]
    assert sources["2"]["name"] == "Sonos"
    assert not sources["6"][CONF_ENABLED]
    assert result["result"].unique_id == BRIDGE_ID


async def test_user_cannot_connect(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """Nothing listening: form error, then a retry succeeds."""
    port = fake_bridge.port
    await fake_bridge.stop()
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": SOURCE_USER}
    )
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], {CONF_HOST: "127.0.0.1", CONF_PORT: port}
    )
    assert result["type"] is FlowResultType.FORM
    assert result["errors"] == {"base": "cannot_connect"}

    new_port = await fake_bridge.start()
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], {CONF_HOST: "127.0.0.1", CONF_PORT: new_port}
    )
    assert result["step_id"] == "zones"


async def test_user_invalid_auth_then_token(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """A bridge with a token: none fails, the right one works and is stored."""
    fake_bridge.token = "s3cret"
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": SOURCE_USER}
    )
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], {CONF_HOST: "127.0.0.1", CONF_PORT: fake_bridge.port}
    )
    assert result["errors"] == {"base": "invalid_auth"}
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"],
        {CONF_HOST: "127.0.0.1", CONF_PORT: fake_bridge.port, CONF_TOKEN: "s3cret"},
    )
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], _zones_input()
    )
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], _sources_input()
    )
    assert result["type"] is FlowResultType.CREATE_ENTRY
    assert result["data"][CONF_TOKEN] == "s3cret"


async def test_user_unsupported_proto(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """A bridge on another proto is refused with a clear error."""
    fake_bridge.proto = 2
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": SOURCE_USER}
    )
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], {CONF_HOST: "127.0.0.1", CONF_PORT: fake_bridge.port}
    )
    assert result["errors"] == {"base": "unsupported_proto"}


async def test_user_already_configured_updates_host(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """Adding the same bridge again aborts and refreshes the stored address."""
    entry = make_entry(1)
    entry.add_to_hass(hass)
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": SOURCE_USER}
    )
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], {CONF_HOST: "127.0.0.1", CONF_PORT: fake_bridge.port}
    )
    assert result["type"] is FlowResultType.ABORT
    assert result["reason"] == "already_configured"
    assert entry.data[CONF_PORT] == fake_bridge.port


async def test_zeroconf_flow(hass: HomeAssistant, fake_bridge: FakeBridge) -> None:
    """Discovery -> confirm -> zones -> sources -> entry."""
    result = await hass.config_entries.flow.async_init(
        DOMAIN,
        context={"source": SOURCE_ZEROCONF},
        data=_discovery(fake_bridge.port),
    )
    assert result["type"] is FlowResultType.FORM
    assert result["step_id"] == "zeroconf_confirm"
    assert result["description_placeholders"] == {"host": "127.0.0.1", "id": BRIDGE_ID}

    result = await hass.config_entries.flow.async_configure(result["flow_id"], {})
    assert result["step_id"] == "zones"
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], _zones_input()
    )
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], _sources_input()
    )
    assert result["type"] is FlowResultType.CREATE_ENTRY
    assert result["result"].unique_id == BRIDGE_ID
    assert result["data"][CONF_PORT] == fake_bridge.port


async def test_zeroconf_with_token(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """A discovered bridge that needs a token asks for it on the confirm form."""
    fake_bridge.token = "s3cret"
    result = await hass.config_entries.flow.async_init(
        DOMAIN,
        context={"source": SOURCE_ZEROCONF},
        data=_discovery(fake_bridge.port),
    )
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {})
    assert result["errors"] == {"base": "invalid_auth"}
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], {CONF_TOKEN: "s3cret"}
    )
    assert result["step_id"] == "zones"


async def test_zeroconf_rediscovery_updates_host(
    hass: HomeAssistant, fake_bridge: FakeBridge
) -> None:
    """A known bridge at a new address (Ethernet <-> Wi-Fi) updates the entry."""
    entry = make_entry(1)
    entry.add_to_hass(hass)
    result = await hass.config_entries.flow.async_init(
        DOMAIN,
        context={"source": SOURCE_ZEROCONF},
        data=_discovery(fake_bridge.port),
    )
    assert result["type"] is FlowResultType.ABORT
    assert result["reason"] == "already_configured"
    assert entry.data[CONF_PORT] == fake_bridge.port


@pytest.mark.parametrize(
    ("props", "reason"),
    [({"proto": "2"}, "unsupported_proto"), ({"id": ""}, "invalid_discovery")],
)
async def test_zeroconf_aborts(
    hass: HomeAssistant, fake_bridge: FakeBridge, props: dict[str, str], reason: str
) -> None:
    """Discovery with another proto, or no id, aborts."""
    result = await hass.config_entries.flow.async_init(
        DOMAIN,
        context={"source": SOURCE_ZEROCONF},
        data=_discovery(fake_bridge.port, **props),
    )
    assert result["type"] is FlowResultType.ABORT
    assert result["reason"] == reason
