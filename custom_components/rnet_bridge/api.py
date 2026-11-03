"""WebSocket/HTTP client for the RNET Bridge firmware (docs/protocol.md, proto 1).

Uses Home Assistant's aiohttp session; no external requirements (SPEC §5.1).
"""

from __future__ import annotations

import asyncio
from collections.abc import Callable
from dataclasses import dataclass, field
import json
import logging
from typing import Any

import aiohttp

from .const import (
    COMMAND_TIMEOUT_S,
    CONNECT_TIMEOUT_S,
    LISTEN_TIMEOUT_S,
    NUM_ZONES,
    SUPPORTED_PROTO,
)

_LOGGER = logging.getLogger(__name__)


class RnetBridgeError(Exception):
    """Base error."""


class RnetBridgeConnectionError(RnetBridgeError):
    """The bridge could not be reached, or the connection dropped."""


class RnetBridgeAuthError(RnetBridgeError):
    """The bridge rejected the API token, or needs one."""


class RnetBridgeProtocolError(RnetBridgeError):
    """The bridge speaks an unsupported `proto`, or sent something unexpected."""

    def __init__(self, message: str, proto: int | None = None) -> None:
        """Keep the bridge's proto so the caller can report it."""
        super().__init__(message)
        self.proto = proto


class RnetBridgeCommandError(RnetBridgeError):
    """A command was rejected: `error` is busy, invalid, link_down or timeout."""

    def __init__(self, error: str) -> None:
        """Keep the error name for translation."""
        super().__init__(error)
        self.error = error


@dataclass(frozen=True, slots=True)
class BridgeInfo:
    """`hello` plus the HTTP /api/info fields we use."""

    bridge_id: str
    fw: str
    proto: int
    auth_required: bool


@dataclass(frozen=True, slots=True)
class ZoneState:
    """One zone, as in `zone_state` (1-based zone and source)."""

    zone: int
    valid: bool
    power: bool
    source: int
    volume: int
    muted: bool
    bass: int
    treble: int
    balance: int
    loudness: bool
    turn_on_volume: int | None
    shared_source: bool
    system_on: bool

    @classmethod
    def from_json(cls, data: dict[str, Any]) -> ZoneState:
        """Parse a `zone_state` message or a snapshot entry."""
        return cls(
            zone=int(data["zone"]),
            valid=bool(data["valid"]),
            power=bool(data["power"]),
            source=int(data["source"]),
            volume=int(data["volume"]),
            muted=bool(data["muted"]),
            bass=int(data["bass"]),
            treble=int(data["treble"]),
            balance=int(data["balance"]),
            loudness=bool(data["loudness"]),
            turn_on_volume=(
                None
                if data.get("turn_on_volume") is None
                else int(data["turn_on_volume"])
            ),
            shared_source=bool(data["shared_source"]),
            system_on=bool(data["system_on"]),
        )


@dataclass(frozen=True, slots=True)
class LinkState:
    """The `link` message: RNET link up/down plus engine counters."""

    up: bool
    stats: dict[str, int] = field(default_factory=dict)

    @classmethod
    def from_json(cls, data: dict[str, Any]) -> LinkState:
        """Parse a `link` message or the snapshot's link object."""
        return cls(
            up=bool(data["up"]),
            stats={k: int(v) for k, v in data.get("stats", {}).items()},
        )


@dataclass(frozen=True, slots=True)
class Snapshot:
    """The `snapshot` sent after hello/auth."""

    link: LinkState
    zones: dict[int, ZoneState]


type MessageCallback = Callable[[ZoneState | LinkState], None]


async def async_get_info(
    session: aiohttp.ClientSession, host: str, port: int
) -> dict[str, Any]:
    """GET /api/info."""
    try:
        async with asyncio.timeout(CONNECT_TIMEOUT_S):
            resp = await session.get(f"http://{host}:{port}/api/info")
            resp.raise_for_status()
            data: dict[str, Any] = await resp.json()
    except (aiohttp.ClientError, TimeoutError) as err:
        raise RnetBridgeConnectionError(str(err) or type(err).__name__) from err
    return data


class RnetBridgeClient:
    """One WebSocket session with the bridge.

    connect() does hello, the proto check, auth and the snapshot. listen() then
    delivers zone_state/link messages to the callback until the connection drops,
    and resolves command results. A new connection needs a new connect().
    """

    def __init__(
        self,
        session: aiohttp.ClientSession,
        host: str,
        port: int,
        token: str | None = None,
    ) -> None:
        """Store the connection settings; nothing is opened yet."""
        self._session = session
        self._host = host
        self._port = port
        self._token = token or None
        self._ws: aiohttp.ClientWebSocketResponse | None = None
        self._next_id = 1
        self._pending: dict[int, asyncio.Future[None]] = {}
        self.info: BridgeInfo | None = None

    @property
    def connected(self) -> bool:
        """True while the WebSocket is open."""
        return self._ws is not None and not self._ws.closed

    async def connect(self) -> Snapshot:
        """Open the WebSocket and return the snapshot."""
        await self.close()
        url = f"http://{self._host}:{self._port}/ws"
        try:
            async with asyncio.timeout(CONNECT_TIMEOUT_S):
                # No aiohttp heartbeat: the bridge pings us (autoping answers) and
                # sends `link` every 30 s, which listen() uses as the liveness check.
                self._ws = await self._session.ws_connect(url)
                hello = await self._receive_json()
                if hello.get("type") != "hello":
                    raise RnetBridgeProtocolError(
                        f"expected hello, got {hello.get('type')}"
                    )
                proto = int(hello.get("proto", 0))
                if proto != SUPPORTED_PROTO:
                    raise RnetBridgeProtocolError(
                        f"bridge speaks proto {proto}, expected {SUPPORTED_PROTO}",
                        proto,
                    )
                auth_required = bool(hello.get("auth_required", False))
                if auth_required:
                    if self._token is None:
                        raise RnetBridgeAuthError("the bridge requires an API token")
                    await self._ws.send_json({"type": "auth", "token": self._token})
                first = await self._receive_json()
                if first.get("type") == "error":
                    raise RnetBridgeAuthError(str(first.get("error")))
                if first.get("type") != "snapshot":
                    raise RnetBridgeProtocolError(
                        f"expected snapshot, got {first.get('type')}"
                    )
        except (aiohttp.ClientError, TimeoutError, ConnectionError) as err:
            await self.close()
            raise RnetBridgeConnectionError(str(err) or type(err).__name__) from err
        except RnetBridgeError:
            await self.close()
            raise

        self.info = BridgeInfo(
            bridge_id=str(hello["id"]),
            fw=str(hello.get("fw", "")),
            proto=proto,
            auth_required=auth_required,
        )
        zones = {z.zone: z for z in (ZoneState.from_json(d) for d in first["zones"])}
        if set(zones) != set(range(1, NUM_ZONES + 1)):
            await self.close()
            raise RnetBridgeProtocolError("snapshot must list zones 1-6")
        return Snapshot(link=LinkState.from_json(first["link"]), zones=zones)

    async def _receive_json(self) -> dict[str, Any]:
        assert self._ws is not None
        msg = await self._ws.receive()
        if msg.type in (
            aiohttp.WSMsgType.CLOSE,
            aiohttp.WSMsgType.CLOSED,
            aiohttp.WSMsgType.CLOSING,
            aiohttp.WSMsgType.ERROR,
        ):
            raise RnetBridgeConnectionError("connection closed by the bridge")
        if msg.type != aiohttp.WSMsgType.TEXT:
            raise RnetBridgeProtocolError(f"unexpected WebSocket message {msg.type}")
        try:
            data = json.loads(msg.data)
        except ValueError as err:
            raise RnetBridgeProtocolError("invalid JSON from the bridge") from err
        if not isinstance(data, dict):
            raise RnetBridgeProtocolError("bridge message is not a JSON object")
        return data

    async def listen(self, callback: MessageCallback) -> None:
        """Deliver pushes until the connection drops, then raise.

        Always raises RnetBridgeConnectionError at the end, and fails any
        command still waiting for its result.
        """
        if self._ws is None:
            raise RnetBridgeConnectionError("not connected")
        try:
            while True:
                try:
                    async with asyncio.timeout(LISTEN_TIMEOUT_S):
                        data = await self._receive_json()
                except TimeoutError as err:
                    raise RnetBridgeConnectionError(
                        f"nothing from the bridge for {LISTEN_TIMEOUT_S:.0f} s"
                    ) from err
                kind = data.get("type")
                if kind == "zone_state":
                    callback(ZoneState.from_json(data))
                elif kind == "link":
                    callback(LinkState.from_json(data))
                elif kind == "result":
                    self._on_result(data)
                elif kind == "error":
                    _LOGGER.warning("Bridge reported an error: %s", data.get("error"))
                # log/frame are only sent on request; anything else is ignored
                # (docs/protocol.md: clients ignore what they don't know).
        except (aiohttp.ClientError, ConnectionError) as err:
            raise RnetBridgeConnectionError(str(err) or type(err).__name__) from err
        finally:
            self._fail_pending("connection lost")
            await self.close()

    def _on_result(self, data: dict[str, Any]) -> None:
        fut = self._pending.pop(int(data.get("id", -1)), None)
        if fut is None or fut.done():
            return
        if data.get("ok"):
            fut.set_result(None)
        else:
            fut.set_exception(RnetBridgeCommandError(str(data.get("error", "invalid"))))

    def _fail_pending(self, reason: str) -> None:
        for fut in self._pending.values():
            if not fut.done():
                fut.set_exception(RnetBridgeConnectionError(reason))
        self._pending.clear()

    async def send_command(
        self, cmd: str, zone: int | None = None, value: bool | int | None = None
    ) -> None:
        """Send a command and wait for its result (docs/protocol.md).

        Needs listen() running to receive the result. Raises
        RnetBridgeCommandError (including "timeout") or
        RnetBridgeConnectionError.
        """
        if not self.connected:
            raise RnetBridgeConnectionError("not connected")
        assert self._ws is not None
        msg_id = self._next_id
        self._next_id = (self._next_id % 0xFFFFFFFF) + 1
        message: dict[str, Any] = {"type": "command", "id": msg_id, "cmd": cmd}
        if zone is not None:
            message["zone"] = zone
        if value is not None:
            message["value"] = value
        fut: asyncio.Future[None] = asyncio.get_running_loop().create_future()
        self._pending[msg_id] = fut
        try:
            await self._ws.send_json(message)
            async with asyncio.timeout(COMMAND_TIMEOUT_S):
                await fut
        except TimeoutError as err:
            raise RnetBridgeCommandError("timeout") from err
        except (aiohttp.ClientError, ConnectionError) as err:
            raise RnetBridgeConnectionError(str(err) or type(err).__name__) from err
        finally:
            self._pending.pop(msg_id, None)

    async def close(self) -> None:
        """Close the WebSocket (safe to call when not connected)."""
        ws, self._ws = self._ws, None
        if ws is not None and not ws.closed:
            await ws.close()
