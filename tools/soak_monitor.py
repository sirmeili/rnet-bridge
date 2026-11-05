"""Soak monitor for the RNET Bridge (SPEC §9 M8, pass criteria in SPEC §10).

Watches one bridge for days and records, to OUT_DIR:
- events.csv: every bridge reset, WebSocket disconnect/reconnect, RNET link change,
  counter increase and liveness check, timestamped (UTC).
- summary.json: totals and pass/fail per criterion, rewritten every SUMMARY_INTERVAL
  and on exit. State persists there, so a restarted monitor continues the same run
  and records the gap.

Settings come from the environment (or the matching --options):
    BRIDGE_HOST (required), BRIDGE_PORT=80, BRIDGE_TOKEN, OUT_DIR=./soak-data,
    INFO_INTERVAL=300, LIVENESS_INTERVAL=600, SUMMARY_INTERVAL=600 (seconds).

Run directly (needs aiohttp) or with the Dockerfile in tools/soak/.
"""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import csv
from dataclasses import asdict, dataclass, field
from datetime import UTC, datetime
import json
import os
from pathlib import Path
import signal
import time
from typing import Any

import aiohttp

PROTO = 1
COUNTERS = ("checksum_errors", "dropped", "timeouts", "retries", "duplicate_replies")
RESULT_TIMEOUT_S = 5.0
STALL_MIN_INTERVAL_S = 25.0  # periodic `link` comes every 30 s
LINK_UPTIME_MIN = 0.999  # SPEC §10 soak criterion


def _now_iso() -> str:
    return datetime.now(UTC).isoformat(timespec="seconds")


@dataclass
class SoakState:
    """Everything summary.json holds; reloaded on restart."""

    host: str = ""
    started: str = ""
    last_update: str = ""
    monitored_s: float = 0.0  # time with the WebSocket connected
    link_up_s: float = 0.0  # of which the RNET link was up
    monitoring_gap_s: float = 0.0  # monitor not running (container restarts)
    monitor_restarts: int = 0
    bridge_resets: int = 0
    ws_connects: int = 0
    ws_disconnects: int = 0
    link_down_events: int = 0
    liveness_ok: int = 0
    liveness_failed: int = 0
    polling_stalls: int = 0
    zone_updates: int = 0
    counter_increases: dict[str, int] = field(
        default_factory=lambda: dict.fromkeys(COUNTERS, 0)
    )
    firmware: str = ""


class SoakMonitor:
    """One monitoring run against one bridge."""

    def __init__(
        self,
        host: str,
        port: int,
        token: str | None,
        out_dir: Path,
        info_interval: float,
        liveness_interval: float,
        summary_interval: float,
        stall_min_interval: float = STALL_MIN_INTERVAL_S,
    ) -> None:
        """Set up; call run() to start."""
        self.host = host
        self.port = port
        self.token = token
        self.out_dir = out_dir
        self.info_interval = info_interval
        self.liveness_interval = liveness_interval
        self.summary_interval = summary_interval
        self.stall_min_interval = stall_min_interval
        self.state = SoakState(host=host)
        self._ws: aiohttp.ClientWebSocketResponse | None = None
        self._connected = False
        self._link_up = False
        self._last_tick = time.monotonic()
        self._last_counters: dict[str, int] | None = None
        self._last_uptime: tuple[float, int] | None = None  # (monotonic, uptime_s)
        self._next_id = 1
        self._pending: dict[int, asyncio.Future[bool]] = {}
        self._last_rx: tuple[float, int] | None = None  # (monotonic, link stats rx)
        self._stop = asyncio.Event()

    # --- persistence ---

    @property
    def _summary_path(self) -> Path:
        return self.out_dir / "summary.json"

    def load(self) -> None:
        """Continue a previous run if summary.json exists."""
        self.out_dir.mkdir(parents=True, exist_ok=True)
        if not self._summary_path.exists():
            self.state.started = _now_iso()
            self.event("monitor_start", host=self.host)
            return
        data = json.loads(self._summary_path.read_text(encoding="utf-8"))
        saved = data.get("state", {})
        for key, value in saved.items():
            if hasattr(self.state, key):
                setattr(self.state, key, value)
        gap = 0.0
        if self.state.last_update:
            last = datetime.fromisoformat(self.state.last_update)
            gap = max(0.0, (datetime.now(UTC) - last).total_seconds())
        self.state.monitoring_gap_s += gap
        self.state.monitor_restarts += 1
        self.event("monitor_restart", gap_s=round(gap))

    def event(self, kind: str, **details: Any) -> None:
        """Append one row to events.csv and print it."""
        path = self.out_dir / "events.csv"
        new = not path.exists()
        with path.open("a", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            if new:
                writer.writerow(["time_utc", "event", "details"])
            writer.writerow([_now_iso(), kind, json.dumps(details, sort_keys=True)])
        print(f"{_now_iso()} {kind} {json.dumps(details, sort_keys=True)}", flush=True)

    def criteria(self) -> dict[str, bool]:
        """Pass/fail per SPEC §10 soak criterion, for the run so far."""
        s = self.state
        ratio = s.link_up_s / s.monitored_s if s.monitored_s else 0.0
        return {
            "no_bridge_resets": s.bridge_resets == 0,
            "no_duplicate_replies": s.counter_increases.get("duplicate_replies", 0)
            == 0,
            "link_up_99_9_percent": ratio >= LINK_UPTIME_MIN,
            "all_liveness_checks_pass": s.liveness_failed == 0 and s.liveness_ok > 0,
            "no_polling_stalls": s.polling_stalls == 0,
            "no_unplanned_ws_disconnects": s.ws_disconnects == 0,
        }

    def write_summary(self) -> None:
        """Rewrite summary.json (atomically)."""
        self._account()
        self.state.last_update = _now_iso()
        s = self.state
        criteria = self.criteria()
        summary = {
            "host": s.host,
            "started": s.started,
            "last_update": s.last_update,
            "monitored_hours": round(s.monitored_s / 3600, 2),
            "link_up_percent": round(100 * s.link_up_s / s.monitored_s, 3)
            if s.monitored_s
            else None,
            "criteria": criteria,
            "pass": all(criteria.values()),
            "state": asdict(s),
        }
        tmp = self._summary_path.with_suffix(".tmp")
        tmp.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        tmp.replace(self._summary_path)

    def _account(self) -> None:
        """Add the time since the last call to the connected / link-up totals."""
        now = time.monotonic()
        elapsed = now - self._last_tick
        self._last_tick = now
        if self._connected:
            self.state.monitored_s += elapsed
            if self._link_up:
                self.state.link_up_s += elapsed

    # --- messages ---

    def _on_link(self, up: bool, stats: dict[str, Any]) -> None:
        self._account()
        if up != self._link_up:
            if not up:
                self.state.link_down_events += 1
            self.event("link_up" if up else "link_down")
        self._link_up = up
        # Not stuck: while the link is up, the engine keeps polling, so the received
        # frame count must grow between periodic `link` messages (every 30 s).
        rx = int(stats.get("rx", 0))
        now = time.monotonic()
        if self._last_rx is not None and up and self._link_up:
            then, prev_rx = self._last_rx
            if now - then >= self.stall_min_interval and rx == prev_rx:
                self.state.polling_stalls += 1
                self.event("polling_stalled", rx=rx, seconds=round(now - then))
        if self._last_rx is None or now - self._last_rx[0] >= self.stall_min_interval:
            self._last_rx = (now, rx)
        counters = {k: int(stats.get(k, 0)) for k in COUNTERS}
        if self._last_counters is not None:
            for key in COUNTERS:
                delta = counters[key] - self._last_counters[key]
                if delta < 0:  # bridge reset: counters restarted from 0
                    delta = counters[key]
                if delta > 0:
                    self.state.counter_increases[key] += delta
                    self.event("counter", name=key, increase=delta, total=counters[key])
        self._last_counters = counters

    def _on_message(self, msg: dict[str, Any]) -> None:
        kind = msg.get("type")
        if kind == "link":
            self._on_link(bool(msg["up"]), msg.get("stats", {}))
        elif kind == "zone_state":
            self.state.zone_updates += 1
        elif kind == "result":
            fut = self._pending.pop(int(msg.get("id", -1)), None)
            if fut is not None and not fut.done():
                fut.set_result(bool(msg.get("ok")))

    # --- loops ---

    async def _connect(self, session: aiohttp.ClientSession) -> None:
        url = f"http://{self.host}:{self.port}/ws"
        self._ws = await session.ws_connect(
            url, timeout=aiohttp.ClientWSTimeout(ws_close=10)
        )
        hello = await self._ws.receive_json(timeout=10)
        if hello.get("type") != "hello" or hello.get("proto") != PROTO:
            raise RuntimeError(f"unexpected hello: {hello}")
        if hello.get("auth_required"):
            await self._ws.send_json({"type": "auth", "token": self.token or ""})
        snapshot = await self._ws.receive_json(timeout=10)
        if snapshot.get("type") != "snapshot":
            raise RuntimeError(f"expected snapshot, got {snapshot.get('type')}")
        self._account()
        # Apply the link state before counting connected time, so the moment between
        # connecting and reading the snapshot isn't counted as link-down.
        self._on_link(bool(snapshot["link"]["up"]), snapshot["link"].get("stats", {}))
        self._connected = True
        self.state.ws_connects += 1
        self.event("ws_connected", fw=hello.get("fw"))
        if hello.get("fw") and hello["fw"] != self.state.firmware:
            if self.state.firmware:
                self.event("firmware_changed", old=self.state.firmware, new=hello["fw"])
            self.state.firmware = str(hello["fw"])

    async def _ws_loop(self, session: aiohttp.ClientSession) -> None:
        delay = 1.0
        while not self._stop.is_set():
            try:
                await self._connect(session)
                delay = 1.0
                assert self._ws is not None
                async for msg in self._ws:
                    if msg.type == aiohttp.WSMsgType.TEXT:
                        self._on_message(json.loads(msg.data))
                    elif msg.type in (
                        aiohttp.WSMsgType.CLOSED,
                        aiohttp.WSMsgType.ERROR,
                    ):
                        break
                reason = "closed by the bridge"
            except (aiohttp.ClientError, TimeoutError, RuntimeError, ValueError) as err:
                reason = f"{type(err).__name__}: {err}"
            if self._connected:
                self._account()
                self._connected = False
                if not self._stop.is_set():  # our own close on shutdown isn't a fault
                    self.state.ws_disconnects += 1
                    self.event("ws_disconnected", reason=reason)
            for fut in self._pending.values():
                if not fut.done():
                    fut.set_result(False)
            self._pending.clear()
            if self._stop.is_set():
                return
            with contextlib.suppress(TimeoutError):
                await asyncio.wait_for(self._stop.wait(), delay)
            delay = min(delay * 2, 30.0)

    async def _info_loop(self, session: aiohttp.ClientSession) -> None:
        while not self._stop.is_set():
            try:
                url = f"http://{self.host}:{self.port}/api/info"
                async with session.get(
                    url, timeout=aiohttp.ClientTimeout(total=10)
                ) as r:
                    info = await r.json()
                uptime = int(info["uptime_s"])
                now = time.monotonic()
                if self._last_uptime is not None:
                    then, prev = self._last_uptime
                    # Uptime should have grown by about the time since the last check.
                    if uptime < prev + (now - then) - 60:
                        self.state.bridge_resets += 1
                        self.event(
                            "bridge_reset", uptime_s=uptime, previous_uptime_s=prev
                        )
                self._last_uptime = (now, uptime)
            except (aiohttp.ClientError, TimeoutError, ValueError, KeyError) as err:
                self.event("info_failed", reason=f"{type(err).__name__}: {err}")
            with contextlib.suppress(TimeoutError):
                await asyncio.wait_for(self._stop.wait(), self.info_interval)

    async def liveness_check(self) -> bool:
        """`refresh` must be answered `ok` (command path and engine alive).

        No zone_state is expected: the bridge pushes only on change.
        """
        if self._ws is None or not self._connected:
            return False
        msg_id = self._next_id
        self._next_id += 1
        fut: asyncio.Future[bool] = asyncio.get_running_loop().create_future()
        self._pending[msg_id] = fut
        try:
            await self._ws.send_json(
                {"type": "command", "id": msg_id, "cmd": "refresh"}
            )
            return await asyncio.wait_for(fut, RESULT_TIMEOUT_S)
        except TimeoutError, aiohttp.ClientError, ConnectionError:
            return False
        finally:
            self._pending.pop(msg_id, None)

    async def _liveness_loop(self) -> None:
        while not self._stop.is_set():
            with contextlib.suppress(TimeoutError):
                await asyncio.wait_for(self._stop.wait(), self.liveness_interval)
            if self._stop.is_set():
                return
            if not self._connected:
                continue  # counted as a disconnect, not a liveness failure
            if await self.liveness_check():
                self.state.liveness_ok += 1
            else:
                self.state.liveness_failed += 1
                self.event("liveness_failed")

    async def _summary_loop(self) -> None:
        while not self._stop.is_set():
            with contextlib.suppress(TimeoutError):
                await asyncio.wait_for(self._stop.wait(), self.summary_interval)
            self.write_summary()

    def stop(self) -> None:
        """Ask run() to finish (writes the final summary)."""
        self._stop.set()

    async def run(self) -> None:
        """Monitor until stop()."""
        self.load()
        self.write_summary()
        async with aiohttp.ClientSession() as session:
            tasks = [
                asyncio.create_task(self._ws_loop(session)),
                asyncio.create_task(self._info_loop(session)),
                asyncio.create_task(self._liveness_loop()),
                asyncio.create_task(self._summary_loop()),
            ]
            await self._stop.wait()
            if self._ws is not None:
                await self._ws.close()
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
        self.write_summary()
        passed = all(self.criteria().values())
        self.event("monitor_stop", passed=passed)


def main() -> None:
    """Command-line / container entry point."""
    env = os.environ
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default=env.get("BRIDGE_HOST"))
    parser.add_argument("--port", type=int, default=int(env.get("BRIDGE_PORT", "80")))
    parser.add_argument("--token", default=env.get("BRIDGE_TOKEN") or None)
    parser.add_argument("--out", default=env.get("OUT_DIR", "soak-data"))
    parser.add_argument(
        "--info-interval", type=float, default=float(env.get("INFO_INTERVAL", "300"))
    )
    parser.add_argument(
        "--liveness-interval",
        type=float,
        default=float(env.get("LIVENESS_INTERVAL", "600")),
    )
    parser.add_argument(
        "--summary-interval",
        type=float,
        default=float(env.get("SUMMARY_INTERVAL", "600")),
    )
    args = parser.parse_args()
    if not args.host:
        parser.error("set BRIDGE_HOST (or --host)")
    monitor = SoakMonitor(
        args.host,
        args.port,
        args.token,
        Path(args.out),
        args.info_interval,
        args.liveness_interval,
        args.summary_interval,
    )

    async def runner() -> None:
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            with contextlib.suppress(NotImplementedError):  # Windows
                loop.add_signal_handler(sig, monitor.stop)
        await monitor.run()

    with contextlib.suppress(KeyboardInterrupt):
        asyncio.run(runner())


if __name__ == "__main__":
    main()
