"""Push coordinator: owns the bridge connection and the latest state (SPEC §5.2)."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass, replace
import logging
from typing import TYPE_CHECKING

from homeassistant.const import CONF_HOST, CONF_PORT, CONF_TOKEN
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import issue_registry as ir
from homeassistant.helpers.aiohttp_client import async_get_clientsession
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
from .const import (
    DOMAIN,
    MOVED_CHECK_EVERY,
    RECONNECT_MAX_S,
    RECONNECT_MIN_S,
    SUPPORTED_PROTO,
)
from .discovery import async_find_bridge

if TYPE_CHECKING:
    from . import RnetConfigEntry


def _proto_issue_id(entry_id: str) -> str:
    return f"unsupported_proto_{entry_id}"


def async_create_proto_issue(
    hass: HomeAssistant, entry: RnetConfigEntry, proto: int | None
) -> None:
    """Repair issue: firmware and integration speak different `proto` (SPEC §4.5)."""
    ir.async_create_issue(
        hass,
        DOMAIN,
        _proto_issue_id(entry.entry_id),
        is_fixable=False,
        severity=ir.IssueSeverity.ERROR,
        translation_key="unsupported_proto",
        translation_placeholders={
            "title": entry.title,
            "proto": str(proto),
            "supported": str(SUPPORTED_PROTO),
        },
    )


def async_delete_proto_issue(hass: HomeAssistant, entry: RnetConfigEntry) -> None:
    """Clear the proto repair issue once the bridge connects."""
    ir.async_delete_issue(hass, DOMAIN, _proto_issue_id(entry.entry_id))


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
            failures = 0
            while True:
                await asyncio.sleep(delay)
                try:
                    snapshot = await self.client.connect()
                except RnetBridgeConnectionError as err:
                    _LOGGER.debug("Reconnect failed (retry in %.0f s): %s", delay, err)
                    failures += 1
                    if failures % MOVED_CHECK_EVERY == 0 and await self._async_moved():
                        return  # the entry reloads at the new address
                except RnetBridgeAuthError as err:
                    # The token changed on the bridge: ask the user (reauth flow).
                    _LOGGER.error("RNET Bridge rejected the API token: %s", err)
                    self.config_entry.async_start_reauth(self.hass)
                    delay = RECONNECT_MAX_S
                except RnetBridgeProtocolError as err:
                    # Firmware and integration disagree on `proto`: repair issue.
                    _LOGGER.error("RNET Bridge refused the connection: %s", err)
                    async_create_proto_issue(self.hass, self.config_entry, err.proto)
                    delay = RECONNECT_MAX_S
                else:
                    _LOGGER.info("Reconnected to the RNET Bridge")
                    async_delete_proto_issue(self.hass, self.config_entry)
                    self._apply_snapshot(snapshot)
                    delay = RECONNECT_MIN_S
                    break
                delay = min(delay * 2, RECONNECT_MAX_S)

    async def _async_moved(self) -> bool:
        """Look the bridge up over mDNS; if it answers at another address, save
        that address (the update listener reloads the entry) and return True."""
        token = self.config_entry.data.get(CONF_TOKEN)
        current = (self.client.host, self.client.port)
        for host, port in await async_find_bridge(self.hass, self.bridge_id):
            if (host, port) == current:
                continue
            probe = RnetBridgeClient(
                async_get_clientsession(self.hass), host, port, token
            )
            try:
                await probe.connect()
            except RnetBridgeError:
                continue
            finally:
                await probe.close()
            _LOGGER.info("RNET Bridge moved to %s:%s (was %s:%s)", host, port, *current)
            self.hass.config_entries.async_update_entry(
                self.config_entry,
                data={**self.config_entry.data, CONF_HOST: host, CONF_PORT: port},
            )
            return True
        return False

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

    async def async_send_raw(self, hex_bytes: str, fix_checksum: bool) -> None:
        """Debug raw send (the send_raw service); translated errors like async_send."""
        try:
            await self.client.send_raw(hex_bytes, fix_checksum)
        except RnetBridgeCommandError as err:
            raise HomeAssistantError(
                translation_domain=DOMAIN,
                translation_key=f"command_{err.error}"
                if err.error in ("busy", "invalid", "link_down", "timeout")
                else "command_failed",
                translation_placeholders={"command": "send_raw"},
            ) from err
        except RnetBridgeConnectionError as err:
            raise HomeAssistantError(
                translation_domain=DOMAIN,
                translation_key="not_connected",
                translation_placeholders={"command": "send_raw"},
            ) from err

    async def async_shutdown(self) -> None:
        """Stop the coordinator and close the connection."""
        await super().async_shutdown()
        await self.client.close()
