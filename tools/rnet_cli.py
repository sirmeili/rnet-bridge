"""Talk to an RNET Bridge from a terminal (docs/protocol.md, proto 1).

Examples:
    python tools/rnet_cli.py discover
    python tools/rnet_cli.py info rnet-bridge-e5f6.local
    python tools/rnet_cli.py watch 192.168.1.50            # snapshot, then live zone/link updates
    python tools/rnet_cli.py cmd 192.168.1.50 set_volume 1 30
    python tools/rnet_cli.py cmd 192.168.1.50 set_power 2 true
    python tools/rnet_cli.py cmd 192.168.1.50 refresh
    python tools/rnet_cli.py logs 192.168.1.50 --level debug
    python tools/rnet_cli.py frames 192.168.1.50
    python tools/rnet_cli.py raw 192.168.1.50 "F0 00 00 7F 00 00 70 01 04 02 00 00 07 00 00" --fix

Pass --token if the bridge has an API token. Requires aiohttp (tools/requirements.txt).
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
import time
from typing import Any

import aiohttp

PROTO = 1
RESULT_TIMEOUT_S = 5.0


def _ts() -> str:
    return time.strftime("%H:%M:%S")


def _zone_line(z: dict[str, Any]) -> str:
    if not z.get("valid"):
        return f"z{z['zone']} (not polled yet)"
    tov = z.get("turn_on_volume")
    return (
        f"z{z['zone']} {'on ' if z['power'] else 'off'} src={z['source']} vol={z['volume']:>2}"
        f"{' MUTED' if z['muted'] else ''} bass={z['bass']} treble={z['treble']} bal={z['balance']}"
        f" loud={'on' if z['loudness'] else 'off'} tov={'?' if tov is None else tov}"
        f" sys={'on' if z['system_on'] else 'off'}"
    )


def _print_message(msg: dict[str, Any]) -> None:
    kind = msg.get("type")
    if kind == "snapshot":
        print(f"{_ts()} snapshot: link {'UP' if msg['link']['up'] else 'DOWN'}")
        for z in msg["zones"]:
            print(f"{_ts()}   {_zone_line(z)}")
    elif kind == "zone_state":
        print(f"{_ts()} {_zone_line(msg)}")
    elif kind == "link":
        s = msg["stats"]
        print(
            f"{_ts()} link {'UP' if msg['up'] else 'DOWN'} tx={s['tx']} rx={s['rx']}"
            f" checksum_errors={s['checksum_errors']} timeouts={s['timeouts']}"
            f" retries={s['retries']} duplicate_replies={s['duplicate_replies']}"
            f" queue_depth={s['queue_depth']}"
        )
    elif kind == "result":
        status = "ok" if msg["ok"] else f"FAILED: {msg.get('error')}"
        print(f"{_ts()} result {msg['id']}: {status}")
    elif kind == "log":
        print(f"{_ts()} [{msg['level']}] {msg['msg']}")
    elif kind == "frame":
        print(f"{_ts()} {msg['dir']} {msg['hex']}")
    else:
        print(f"{_ts()} {json.dumps(msg)}")


def _base_url(host: str, port: int) -> str:
    return f"http://{host}:{port}"


class Session:
    """A WebSocket session: hello, optional auth, then the snapshot."""

    def __init__(self, ws: aiohttp.ClientWebSocketResponse) -> None:
        self.ws = ws
        self.next_id = 1

    async def receive(self, timeout: float | None = None) -> dict[str, Any] | None:
        msg = await self.ws.receive(timeout=timeout)
        if msg.type != aiohttp.WSMsgType.TEXT:
            return None
        data: dict[str, Any] = json.loads(msg.data)
        return data

    async def start(self, token: str | None) -> dict[str, Any]:
        hello = await self.receive(timeout=5)
        if hello is None or hello.get("type") != "hello":
            raise RuntimeError(f"expected hello, got {hello}")
        if hello.get("proto") != PROTO:
            raise RuntimeError(
                f"bridge speaks proto {hello.get('proto')}, this tool speaks {PROTO}"
            )
        if hello.get("auth_required"):
            if not token:
                raise RuntimeError("the bridge needs --token")
            await self.ws.send_json({"type": "auth", "token": token})
        snapshot = await self.receive(timeout=5)
        if snapshot is None or snapshot.get("type") != "snapshot":
            raise RuntimeError(f"expected snapshot, got {snapshot}")
        return snapshot

    async def request(self, message: dict[str, Any]) -> dict[str, Any]:
        """Sends a command or raw_send and waits for its result, printing anything else."""
        msg_id = self.next_id
        self.next_id += 1
        await self.ws.send_json({**message, "id": msg_id})
        deadline = time.monotonic() + RESULT_TIMEOUT_S
        while (left := deadline - time.monotonic()) > 0:
            msg = await self.receive(timeout=left)
            if msg is None:
                break
            if msg.get("type") == "result" and msg.get("id") == msg_id:
                return msg
            _print_message(msg)
        raise RuntimeError(f"no result for id {msg_id} within {RESULT_TIMEOUT_S} s")


def _parse_value(text: str) -> bool | int:
    lowered = text.lower()
    if lowered in ("true", "on"):
        return True
    if lowered in ("false", "off"):
        return False
    return int(text)


async def _run(args: argparse.Namespace) -> int:
    url = _base_url(args.host, args.port)
    async with aiohttp.ClientSession() as http:
        if args.action == "info":
            async with http.get(
                f"{url}/api/info", timeout=aiohttp.ClientTimeout(total=5)
            ) as r:
                print(json.dumps(await r.json(), indent=2))
            return 0

        async with http.ws_connect(f"{url}/ws", heartbeat=15) as ws:
            session = Session(ws)
            snapshot = await session.start(args.token)
            if args.action in ("watch", "logs", "frames"):
                _print_message(snapshot)
                if args.action == "logs":
                    await ws.send_json({"type": "subscribe_logs", "level": args.level})
                if args.action == "frames":
                    await ws.send_json({"type": "trace_frames", "enabled": True})
                while (msg := await session.receive()) is not None:
                    _print_message(msg)
                print(f"{_ts()} connection closed")
                return 1

            if args.action == "cmd":
                message: dict[str, Any] = {"type": "command", "cmd": args.cmd}
                if args.zone is not None:
                    message["zone"] = args.zone
                if args.value is not None:
                    message["value"] = _parse_value(args.value)
            else:  # raw
                message = {
                    "type": "raw_send",
                    "hex": args.hex,
                    "fix_checksum": args.fix,
                }
            result = await session.request(message)
            _print_message(result)
            if result["ok"]:
                # The confirming zone_state follows the priority poll about 150 ms later.
                end = time.monotonic() + 1.0
                while (left := end - time.monotonic()) > 0:
                    try:
                        msg = await session.receive(timeout=left)
                    except TimeoutError:
                        break
                    if msg is None:
                        break
                    _print_message(msg)
            return 0 if result["ok"] else 1


def _discover(seconds: float) -> int:
    try:
        from zeroconf import ServiceBrowser, ServiceListener, Zeroconf
    except ImportError:
        print(
            "discover needs zeroconf: pip install -r tools/requirements.txt",
            file=sys.stderr,
        )
        return 2

    found: list[str] = []

    class Listener(ServiceListener):
        def add_service(self, zc: Zeroconf, type_: str, name: str) -> None:
            info = zc.get_service_info(type_, name)
            if info is None:
                return
            txt = {k.decode(): (v or b"").decode() for k, v in info.properties.items()}
            addrs = ", ".join(info.parsed_addresses())
            line = f"{info.server} {addrs}:{info.port} {txt}"
            found.append(line)
            print(line)

        def update_service(self, zc: Zeroconf, type_: str, name: str) -> None:
            self.add_service(zc, type_, name)

        def remove_service(self, zc: Zeroconf, type_: str, name: str) -> None:
            print(f"removed {name}")

    zc = Zeroconf()
    try:
        ServiceBrowser(zc, "_rnetbridge._tcp.local.", Listener())
        time.sleep(seconds)
    finally:
        zc.close()
    if not found:
        print("no bridges found")
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = parser.add_subparsers(dest="action", required=True)

    disc = sub.add_parser("discover", help="browse mDNS for bridges")
    disc.add_argument("--seconds", type=float, default=3.0)

    def host_parser(name: str, help_text: str) -> argparse.ArgumentParser:
        p = sub.add_parser(name, help=help_text)
        p.add_argument("host")
        p.add_argument("--port", type=int, default=80)
        p.add_argument("--token", default=None)
        return p

    host_parser("info", "GET /api/info")
    host_parser("watch", "print the snapshot, then live updates")
    logs = host_parser("logs", "watch, plus bridge log lines")
    logs.add_argument(
        "--level", default="info", choices=["debug", "info", "warn", "error"]
    )
    host_parser("frames", "watch, plus every RNET frame sent and received")
    cmd = host_parser("cmd", "send one command and show its result")
    cmd.add_argument("cmd")
    cmd.add_argument("zone", nargs="?", type=int)
    cmd.add_argument("value", nargs="?")
    raw = host_parser("raw", "send raw RNET bytes (debug)")
    raw.add_argument("hex")
    raw.add_argument("--fix", action="store_true", help="append checksum and F7")

    args = parser.parse_args()
    if args.action == "discover":
        return _discover(args.seconds)
    try:
        return asyncio.run(_run(args))
    except (RuntimeError, aiohttp.ClientError, TimeoutError) as err:
        print(f"error: {err}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
