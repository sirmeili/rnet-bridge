"""Fixtures: the in-process fake bridge (tools/fake_bridge.py) and config entries."""

from __future__ import annotations

from collections.abc import AsyncGenerator, Generator
from pathlib import Path
import sys
from typing import Any
from unittest.mock import AsyncMock, MagicMock, patch

from homeassistant.const import CONF_HOST, CONF_PORT
import pytest
from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.rnet_bridge.const import (
    CONF_ENABLED,
    CONF_SOURCES,
    CONF_ZONES,
    DOMAIN,
)

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from fake_bridge import FakeBridge

BRIDGE_ID = "a1b2c3d4e5f6"


@pytest.fixture(autouse=True)
def auto_enable_custom_integrations(
    enable_custom_integrations: None,
) -> None:
    """Load custom_components/ in every test."""


@pytest.fixture(autouse=True)
def mock_zeroconf_and_lookup(mock_async_zeroconf: MagicMock) -> Generator[AsyncMock]:
    """No real mDNS: zeroconf is mocked, and the moved-bridge lookup finds nothing
    unless a test sets `return_value`."""
    lookup = AsyncMock(return_value=[])
    with (
        patch("custom_components.rnet_bridge.async_find_bridge", lookup),
        patch("custom_components.rnet_bridge.coordinator.async_find_bridge", lookup),
    ):
        yield lookup


@pytest.fixture
async def fake_bridge(socket_enabled: None) -> AsyncGenerator[FakeBridge]:
    """A fake bridge listening on 127.0.0.1 (no token)."""
    bridge = FakeBridge(bridge_id=BRIDGE_ID)
    await bridge.start()
    yield bridge
    await bridge.stop()


def default_options() -> dict[str, Any]:
    """Options as the config flow creates them with the defaults."""
    return {
        CONF_ZONES: {
            str(n): {CONF_ENABLED: True, "name": f"Zone {n}"} for n in range(1, 7)
        },
        CONF_SOURCES: {
            str(n): {CONF_ENABLED: True, "name": f"Source {n}"} for n in range(1, 7)
        },
    }


def make_entry(
    port: int, options: dict[str, Any] | None = None, **data: Any
) -> MockConfigEntry:
    """A config entry for a bridge on 127.0.0.1:port."""
    return MockConfigEntry(
        domain=DOMAIN,
        unique_id=BRIDGE_ID,
        title=f"RNET Bridge {BRIDGE_ID[-4:]}",
        data={CONF_HOST: "127.0.0.1", CONF_PORT: port, **data},
        options=options if options is not None else default_options(),
    )
