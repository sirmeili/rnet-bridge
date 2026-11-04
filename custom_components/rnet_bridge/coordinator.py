"""Push coordinator: owns the bridge connection and the latest state (SPEC §5.2)."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass, replace
import logging
from typing import TYPE_CHECKING

from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator

from .api import (
    LinkState,
    RnetBridgeAuthError,
    RnetBridgeClient,
    RnetBridgeCommandError,
    RnetBridgeConnectionError,
    RnetBridgeError,
    RnetBridgeProtocolError,
    Snapshot,
    ZoneState,
)
from .const import DOMAIN, RECONNECT_MAX_S, RECONNECT_MIN_S

if TYPE_CHECKING:
    from . import RnetConfigEntry

_LOGGER = logging.getLogger(__name__)


@dataclass(frozen=True, slots=True)
class BridgeData:
    """Everything entities read. Replaced (not mutated) on every update."""

    connected: bool
    link: LinkState
    zones: dict[int, ZoneState]

    @property
    def available(self) -> bool:
        """WebSocket connected and RNET link up (SPEC §5.4)."""
        return self.connected and self.link.up


class RnetBridgeCoordinator(DataUpdateCoordinator[BridgeData]):
    """Holds the client, applies pushes, and reconnects with backoff.

    No polling: the bridge pushes zone_state and link (update_interval=None).
    """

    config_entry: RnetConfigEntry

    def __init__(
        self, hass: HomeAssistant, entry: RnetConfigEntry, client: RnetBridgeClient
    ) -> None:
        """Set up without connecting."""
        super().__init__(
            hass, _LOGGER, config_entry=entry, name=DOMAIN, update_interval=None
        )
        self.client = client
        # Device registry id of the controller device, set by async_setup_entry;
        # zone devices link to it (SPEC §5.4).
        self.controller_device_id: str = ""

    @property
    def bridge_id(self) -> str:
        """The bridge's stable id (Ethernet MAC), known after the first connect."""
        assert self.client.info is not None
        return self.client.info.bridge_id

    @property
    def firmware(self) -> str:
        """Firmware version reported in hello."""
        assert self.client.info is not None
        return self.client.info.fw

    async def async_connect(self) -> None:
        """First connection. Raises RnetBridgeError for async_setup_entry to map."""
        snapshot = await self.client.connect()
        self._apply_snapshot(snapshot)

    def start(self) -> None:
        """Start listening (and reconnecting) in the background."""
        self.config_entry.async_create_background_task(
            self.hass, self._run(), f"{DOMAIN} listener {self.config_entry.entry_id}"
        )

    def _apply_snapshot(self, snapshot: Snapshot) -> None:
        self.async_set_updated_data(
            BridgeData(connected=True, link=snapshot.link, zones=snapshot.zones)
        )

    def _on_message(self, message: ZoneState | LinkState) -> None:
        # listen() only runs after a snapshot has set self.data.
        if isinstance(message, ZoneState):
            zones = {**self.data.zones, message.zone: message}
            self.async_set_updated_data(replace(self.data, zones=zones))
        else:
            self.async_set_updated_data(replace(self.data, link=message))

    async def _run(self) -> None:
        """Listen; on any drop, mark disconnected and reconnect with backoff."""
        delay = RECONNECT_MIN_S
        while True:
            try:
                await self.client.listen(self._on_message)
            except RnetBridgeError as err:
                _LOGGER.info("Lost connection to the RNET Bridge: %s", err)
            if self.data is not None and self.data.connected:
                self.async_set_updated_data(replace(self.data, connected=False))
            while True:
                await asyncio.sleep(delay)
                try:
                    snapshot = await self.client.connect()
                except RnetBridgeConnectionError as err:
                    _LOGGER.debug("Reconnect failed (retry in %.0f s): %s", delay, err)
                except (RnetBridgeAuthError, RnetBridgeProtocolError) as err:
                    # Needs the user (token or firmware/integration update). Keep
                    # trying slowly; the repair issue and reauth arrive in M7.
                    _LOGGER.error("RNET Bridge refused the connection: %s", err)
                    delay = RECONNECT_MAX_S
                else:
                    _LOGGER.info("Reconnected to the RNET Bridge")
                    self._apply_snapshot(snapshot)
                    delay = RECONNECT_MIN_S
                    break
                delay = min(delay * 2, RECONNECT_MAX_S)

    async def async_send(
        self, cmd: str, zone: int | None = None, value: bool | int | None = None
    ) -> None:
        """Send a command for an entity; raise a translated HomeAssistantError.

        No optimistic state (CLAUDE.md rule 6): the confirming zone_state
        updates the entities about 150 ms later.
        """
        try:
            await self.client.send_command(cmd, zone, value)
        except RnetBridgeCommandError as err:
            raise HomeAssistantError(
                translation_domain=DOMAIN,
                translation_key=f"command_{err.error}"
                if err.error in ("busy", "invalid", "link_down", "timeout")
                else "command_failed",
                translation_placeholders={"command": cmd},
            ) from err
        except RnetBridgeConnectionError as err:
            raise HomeAssistantError(
                translation_domain=DOMAIN,
                translation_key="not_connected",
                translation_placeholders={"command": cmd},
            ) from err

    async def async_shutdown(self) -> None:
        """Stop the coordinator and close the connection."""
        await super().async_shutdown()
        await self.client.close()
