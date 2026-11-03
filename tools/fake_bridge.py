"""A simulated RNET Bridge implementing the proto 1 API (docs/protocol.md).

Run it to develop the HA integration without hardware:
    python tools/fake_bridge.py --port 8080 [--token s3cret]

The tests import FakeBridge and drive it directly (set_zone, set_link, drop_clients).
It mimics the CAA6.6 behaviour seen on hardware (docs/rnet-notes.md): All On does
nothing, an off zone reports its turn-on volume as its volume, and a confirming
zone_state follows each command about 150 ms later. Requires aiohttp.
"""

from __future__ import annotations

import argparse
import asyncio
from collections.abc import Coroutine
import contextlib
from dataclasses import asdict, dataclass
import json
import logging
import time
from typing import Any

from aiohttp import WSMsgType, web

_LOGGER = logging.getLogger("fake_bridge")

PROTO = 1
FW = "0.0.0-fake"
NUM_ZONES = 6
NUM_SOURCES = 6
PRIORITY_POLL_S = 0.15
LINK_INTERVAL_S = 30.0
UNMUTE_DEFAULT_VOLUME = 10  # SPEC §4.3


@dataclass
class FakeZone:
    """State of one simulated zone (API units)."""

    zone: int
    valid: bool = True
    power: bool = False
    source: int = 1
    volume: int = 20
    muted: bool = False
    bass: int = 0
    treble: int = 0
    balance: int = 0
    loudness: bool = False
    turn_on_volume: int | None = 20
    shared_source: bool = False
    system_on: bool = False


class FakeBridge:
    """An in-process fake bridge: aiohttp server plus simulated controller."""

    def __init__(
        self,
        bridge_id: str = "a1b2c3d4e5f6",
        token: str | None = None,
        proto: int = PROTO,
    ) -> None:
        """Set up six zones; call start() to listen."""
        self.bridge_id = bridge_id
        self.token = token
        self.proto = proto
        self.link_up = True
        self.zones = {z: FakeZone(zone=z) for z in range(1, NUM_ZONES + 1)}
        self.commands: list[dict[str, Any]] = []  # every command received, in order
        self._stored_volume: dict[int, int] = {}
        self._clients: set[web.WebSocketResponse] = set()
        self._runner: web.AppRunner | None = None
        self._started = time.monotonic()
        self._tasks: set[asyncio.Task[None]] = set()
        self.port = 0

    # --- server ---

    async def start(self, host: str = "127.0.0.1", port: int = 0) -> int:
        """Listen on host:port (0 = any free port); return the port."""
        app = web.Application()
        app.router.add_get("/api/info", self._info)
        app.router.add_post("/api/factory_reset", self._factory_reset)
        app.router.add_post("/api/ota", self._ota)
        app.router.add_get("/ws", self._ws)
        self._runner = web.AppRunner(app)
        await self._runner.setup()
        site = web.TCPSite(self._runner, host, port)
        await site.start()
        self._spawn(self._link_ticker())
        server = site._server
        assert server is not None
        sockets = getattr(server, "sockets", None) or []
        self.port = sockets[0].getsockname()[1] if sockets else port
        return self.port

    async def stop(self) -> None:
        """Close all clients and stop listening."""
        await self.drop_clients()
        for task in list(self._tasks):
            task.cancel()
        if self._runner is not None:
            await self._runner.cleanup()
            self._runner = None

    async def drop_clients(self) -> None:
        """Close every WebSocket, as a bridge reboot or network drop would."""
        for ws in list(self._clients):
            await ws.close()
        self._clients.clear()

    # --- test controls ---

    async def set_zone(self, zone: int, **changes: Any) -> None:
        """Change a zone as a keypad would, and push zone_state."""
        z = self.zones[zone]
        for key, value in changes.items():
            setattr(z, key, value)
        self._update_system_on()
        await self._broadcast_zone(zone)

    async def set_link(self, up: bool) -> None:
        """Bring the simulated RNET link up or down, and push link."""
        self.link_up = up
        await self._broadcast(self._link())

    # --- HTTP ---

    def _info_json(self) -> dict[str, Any]:
        return {
            "id": self.bridge_id,
            "name": f"rnet-bridge-{self.bridge_id[-4:]}",
            "fw": FW,
            "proto": self.proto,
            "model": "rnet",
            "zones": NUM_ZONES,
            "sources": NUM_SOURCES,
            "uptime_s": int(time.monotonic() - self._started),
            "network": "ethernet",
            "wifi_rssi": None,
        }

    async def _info(self, request: web.Request) -> web.Response:
        return web.json_response(self._info_json())

    def _authorized(self, request: web.Request) -> bool:
        return not self.token or (
            request.headers.get("Authorization") == f"Bearer {self.token}"
        )

    async def _factory_reset(self, request: web.Request) -> web.Response:
        if not self._authorized(request):
            return web.json_response({"ok": False, "error": "unauthorized"}, status=401)
        return web.json_response({"ok": True})

    async def _ota(self, request: web.Request) -> web.Response:
        if not self._authorized(request):
            return web.json_response({"ok": False, "error": "unauthorized"}, status=401)
        await request.read()
        return web.json_response({"ok": True})

    # --- WebSocket ---

    async def _ws(self, request: web.Request) -> web.WebSocketResponse:
        ws = web.WebSocketResponse()  # no heartbeat timer: keeps test teardown clean
        await ws.prepare(request)
        await ws.send_json(
            {
                "type": "hello",
                "proto": self.proto,
                "id": self.bridge_id,
                "fw": FW,
                "zones": NUM_ZONES,
                "sources": NUM_SOURCES,
                "auth_required": bool(self.token),
            }
        )
        authed = not self.token
        if authed:
            self._clients.add(ws)
            await ws.send_json(self._snapshot())
        try:
            async for msg in ws:
                if msg.type != WSMsgType.TEXT:
                    continue
                try:
                    data = json.loads(msg.data)
                except ValueError:
                    await ws.send_json({"type": "error", "error": "bad_message"})
                    continue
                if not isinstance(data, dict):
                    await ws.send_json({"type": "error", "error": "bad_message"})
                    continue
                if not authed:
                    if data.get("type") == "auth" and data.get("token") == self.token:
                        authed = True
                        self._clients.add(ws)
                        await ws.send_json(self._snapshot())
                    else:
                        await ws.send_json({"type": "error", "error": "unauthorized"})
                        await ws.close()
                    continue
                await self._handle(ws, data)
        finally:
            self._clients.discard(ws)
        return ws

    async def _handle(self, ws: web.WebSocketResponse, data: dict[str, Any]) -> None:
        kind = data.get("type")
        if kind in ("auth", "subscribe_logs", "trace_frames"):
            return
        if kind == "raw_send":
            await ws.send_json({"type": "result", "id": data.get("id"), "ok": True})
            return
        if kind != "command":
            await ws.send_json({"type": "error", "error": "bad_message"})
            return
        self.commands.append(data)
        error = self._apply(data)
        result: dict[str, Any] = {
            "type": "result",
            "id": data.get("id"),
            "ok": error is None,
        }
        if error is not None:
            result["error"] = error
        await ws.send_json(result)

    # --- simulated controller ---

    def _apply(self, c: dict[str, Any]) -> str | None:
        """Apply a command; return an error name or None (docs/protocol.md)."""
        if not isinstance(c.get("id"), int):
            return "invalid"
        # JSON values: checked below before use.
        cmd: Any = c.get("cmd")
        zone_n: Any = c.get("zone")
        value: Any = c.get("value")

        def is_int(v: Any, lo: int, hi: int) -> bool:
            return isinstance(v, int) and not isinstance(v, bool) and lo <= v <= hi

        if cmd in ("all_on", "all_off"):
            if not self.link_up:
                return "link_down"
            if cmd == "all_off":
                for z in self.zones.values():
                    z.power = False
                    z.volume = z.turn_on_volume or z.volume
            self._update_system_on()
            self._confirm(None)
            return None
        if cmd == "refresh":
            if zone_n is not None and not is_int(zone_n, 1, NUM_ZONES):
                return "invalid"
            if not self.link_up:
                return "link_down"
            self._confirm(zone_n)
            return None
        if not is_int(zone_n, 1, NUM_ZONES):
            return "invalid"
        z = self.zones[zone_n]
        bool_cmds = {"set_power", "set_mute", "set_loudness"}
        int_ranges = {
            "set_source": (1, NUM_SOURCES),
            "set_volume": (0, 50),
            "set_bass": (-10, 10),
            "set_treble": (-10, 10),
            "set_balance": (-10, 10),
            "set_turn_on_volume": (0, 50),
        }
        if cmd in bool_cmds:
            if not isinstance(value, bool):
                return "invalid"
        elif cmd in int_ranges:
            lo, hi = int_ranges[cmd]
            if not is_int(value, lo, hi):
                return "invalid"
        elif cmd not in ("volume_up", "volume_down"):
            return "invalid"
        if not self.link_up:
            return "link_down"

        if cmd == "set_power":
            z.power = value
            if not z.power:
                z.volume = z.turn_on_volume or z.volume  # CAA6.6: off zone shows TOV
            elif z.turn_on_volume is not None:
                z.volume = z.turn_on_volume
        elif cmd == "set_source":
            z.source = value
        elif cmd == "set_volume":
            z.volume = value
            if value != 0:
                z.muted = False
        elif cmd in ("volume_up", "volume_down"):
            base = self._stored_volume.get(zone_n, z.volume) if z.muted else z.volume
            z.volume = max(0, min(50, base + (1 if cmd == "volume_up" else -1)))
            z.muted = False
        elif cmd == "set_mute":
            if value and not z.muted:
                self._stored_volume[zone_n] = z.volume
                z.volume = 0
                z.muted = True
            elif not value and z.muted:
                stored = self._stored_volume.get(zone_n, 0)
                z.volume = stored or UNMUTE_DEFAULT_VOLUME
                z.muted = False
        elif cmd == "set_bass":
            z.bass = value
        elif cmd == "set_treble":
            z.treble = value
        elif cmd == "set_balance":
            z.balance = value
        elif cmd == "set_loudness":
            z.loudness = value
        elif cmd == "set_turn_on_volume":
            z.turn_on_volume = value
            if not z.power:
                z.volume = value
        self._update_system_on()
        self._confirm(zone_n)
        return None

    def _update_system_on(self) -> None:
        on = any(z.power for z in self.zones.values())
        for z in self.zones.values():
            z.system_on = on

    def _confirm(self, zone: int | None) -> None:
        """Push zone_state after the priority-poll delay, like the firmware."""

        async def later() -> None:
            await asyncio.sleep(PRIORITY_POLL_S)
            for z in [zone] if zone is not None else list(self.zones):
                await self._broadcast_zone(z)

        self._spawn(later())

    def _spawn(self, coro: Coroutine[Any, Any, None]) -> None:
        task = asyncio.get_running_loop().create_task(coro)
        self._tasks.add(task)
        task.add_done_callback(self._tasks.discard)

    async def _link_ticker(self) -> None:
        """`link` every 30 s, like the firmware (clients use it as a liveness check)."""
        while True:
            await asyncio.sleep(LINK_INTERVAL_S)
            await self._broadcast(self._link())

    def _zone_json(self, zone: int) -> dict[str, Any]:
        return asdict(self.zones[zone])

    def _link(self) -> dict[str, Any]:
        return {
            "type": "link",
            "up": self.link_up,
            "stats": {
                "tx": 0,
                "rx": 0,
                "checksum_errors": 0,
                "dropped": 0,
                "timeouts": 0,
                "retries": 0,
                "duplicate_replies": 0,
                "handshakes_rx": 0,
                "queue_depth": 0,
            },
        }

    def _snapshot(self) -> dict[str, Any]:
        link = self._link()
        del link["type"]
        return {
            "type": "snapshot",
            "link": link,
            "zones": [self._zone_json(z) for z in sorted(self.zones)],
        }

    async def _broadcast_zone(self, zone: int) -> None:
        await self._broadcast({"type": "zone_state", **self._zone_json(zone)})

    async def _broadcast(self, message: dict[str, Any]) -> None:
        for ws in list(self._clients):
            if not ws.closed:
                await ws.send_json(message)


async def _serve(host: str, port: int, token: str | None) -> None:
    bridge = FakeBridge(token=token)
    bridge.zones[1].power = True
    bridge.zones[1].source = 2
    bridge.zones[1].volume = 35
    bridge._update_system_on()
    bound = await bridge.start(host, port)
    print(f"fake bridge on http://{host}:{bound} (proto {PROTO}); Ctrl+C to stop")
    try:
        await asyncio.Event().wait()
    finally:
        await bridge.stop()


def main() -> None:
    """Command-line entry point."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--token", default=None)
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO)
    with contextlib.suppress(KeyboardInterrupt):
        asyncio.run(_serve(args.host, args.port, args.token))


if __name__ == "__main__":
    main()
