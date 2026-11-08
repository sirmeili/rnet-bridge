"""tools/soak_monitor.py against the fake bridge (SPEC §9 M8)."""

from __future__ import annotations

import asyncio
from collections.abc import Callable
import csv
import json
from pathlib import Path
import time

from soak_monitor import SoakMonitor

from .conftest import FakeBridge


async def _wait_for(predicate: Callable[[], bool], within: float = 5.0) -> None:
    async with asyncio.timeout(within):
        while not predicate():  # noqa: ASYNC110
            await asyncio.sleep(0.02)


def _monitor(bridge: FakeBridge, out: Path, **intervals: float) -> SoakMonitor:
    return SoakMonitor(
        "127.0.0.1",
        bridge.port,
        None,
        out,
        intervals.get("info", 0.2),
        intervals.get("liveness", 0.3),
        intervals.get("summary", 0.2),
        intervals.get("stall", 0.3),
    )


def _events(out: Path) -> list[str]:
    with (out / "events.csv").open(encoding="utf-8") as f:
        return [row["event"] for row in csv.DictReader(f)]


async def test_clean_run_passes(fake_bridge: FakeBridge, tmp_path: Path) -> None:
    """Connected, link up, liveness checks pass: every criterion passes."""
    monitor = _monitor(fake_bridge, tmp_path)
    task = asyncio.create_task(monitor.run())
    await _wait_for(lambda: monitor.state.liveness_ok >= 2)
    monitor.stop()
    await task

    summary = json.loads((tmp_path / "summary.json").read_text())
    assert summary["pass"] is True, summary["criteria"]
    assert summary["link_up_percent"] == 100.0
    assert summary["state"]["ws_connects"] == 1
    assert summary["state"]["firmware"] == "0.0.0-fake"
    assert _events(tmp_path)[0] == "monitor_start"
    assert _events(tmp_path)[-1] == "monitor_stop"


async def test_records_disconnect_link_down_and_reset(
    fake_bridge: FakeBridge, tmp_path: Path
) -> None:
    """Each failure is recorded and fails its criterion."""
    monitor = _monitor(fake_bridge, tmp_path)
    task = asyncio.create_task(monitor.run())
    await _wait_for(lambda: monitor.state.ws_connects == 1)
    await asyncio.sleep(0.3)  # let /api/info take a baseline uptime

    await fake_bridge.set_link(False)
    await _wait_for(lambda: monitor.state.link_down_events == 1)
    await fake_bridge.set_link(True)

    await fake_bridge.drop_clients()
    await _wait_for(lambda: monitor.state.ws_disconnects == 1)
    await _wait_for(lambda: monitor.state.ws_connects == 2)

    fake_bridge._started = time.monotonic() + 3600  # uptime jumps back: a reset
    await _wait_for(lambda: monitor.state.bridge_resets == 1)

    monitor.stop()
    await task
    criteria = json.loads((tmp_path / "summary.json").read_text())["criteria"]
    assert criteria["no_bridge_resets"] is False
    assert criteria["no_unplanned_ws_disconnects"] is False
    events = _events(tmp_path)
    for kind in ("link_down", "link_up", "ws_disconnected", "bridge_reset"):
        assert kind in events, kind


async def test_restart_continues_the_run(
    fake_bridge: FakeBridge, tmp_path: Path
) -> None:
    """A restarted monitor keeps the totals and records the gap."""
    first = _monitor(fake_bridge, tmp_path)
    task = asyncio.create_task(first.run())
    await _wait_for(lambda: first.state.liveness_ok >= 1)
    first.stop()
    await task
    started = first.state.started

    second = _monitor(fake_bridge, tmp_path)
    task = asyncio.create_task(second.run())
    await _wait_for(lambda: second.state.ws_connects == 2)
    second.stop()
    await task
    assert second.state.started == started
    assert second.state.monitor_restarts == 1
    assert second.state.liveness_ok >= 1
    assert "monitor_restart" in _events(tmp_path)


async def test_liveness_fails_when_link_is_down(
    fake_bridge: FakeBridge, tmp_path: Path
) -> None:
    """refresh answered link_down counts as a failed liveness check."""
    monitor = _monitor(fake_bridge, tmp_path)
    task = asyncio.create_task(monitor.run())
    await _wait_for(lambda: monitor.state.ws_connects == 1)
    fake_bridge.link_up = False  # without a push, so the monitor still tries
    await _wait_for(lambda: monitor.state.liveness_failed >= 1)
    monitor.stop()
    await task
    assert "liveness_failed" in _events(tmp_path)


async def test_detects_polling_stall(socket_enabled: None, tmp_path: Path) -> None:
    """Link up but the frame count stops growing: a stall is recorded."""
    bridge = FakeBridge(link_interval=0.1)
    await bridge.start()
    monitor = _monitor(bridge, tmp_path)
    task = asyncio.create_task(monitor.run())
    try:
        await asyncio.sleep(1.0)
        assert monitor.state.polling_stalls == 0  # counters growing: fine
        bridge.polling = False
        await _wait_for(lambda: monitor.state.polling_stalls >= 1)
    finally:
        monitor.stop()
        await task
        await bridge.stop()
    summary = json.loads((tmp_path / "summary.json").read_text())
    assert summary["criteria"]["no_polling_stalls"] is False
    assert "polling_stalled" in _events(tmp_path)


async def test_pending_before_any_data(fake_bridge: FakeBridge, tmp_path: Path) -> None:
    """Before anything is monitored or checked, the run is pending, not failed."""
    monitor = _monitor(fake_bridge, tmp_path, liveness=3600)
    assert monitor.passed() is None
    assert monitor.criteria()["link_up_99_9_percent"] is None
    assert monitor.criteria()["all_liveness_checks_pass"] is None
    monitor.state.liveness_failed = 1
    assert monitor.passed() is False
