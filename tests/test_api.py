"""The WebSocket client against the fake bridge (docs/protocol.md)."""

from __future__ import annotations

import asyncio

import aiohttp
import pytest

from custom_components.rnet_bridge.api import (
    LinkState,
    RnetBridgeAuthError,
    RnetBridgeClient,
    RnetBridgeCommandError,
    RnetBridgeConnectionError,
    RnetBridgeProtocolError,
    ZoneState,
    async_get_info,
)

from .conftest import BRIDGE_ID, FakeBridge


async def _listen(client: RnetBridgeClient) -> tuple[asyncio.Task[None], asyncio.Queue]:
    queue: asyncio.Queue[ZoneState | LinkState] = asyncio.Queue()
    task = asyncio.get_running_loop().create_task(client.listen(queue.put_nowait))
    return task, queue


async def _stop(task: asyncio.Task[None]) -> None:
    task.cancel()
    with pytest.raises((asyncio.CancelledError, RnetBridgeConnectionError)):
        await task


async def test_connect_returns_snapshot(fake_bridge: FakeBridge) -> None:
    """Hello, proto check and snapshot."""
    async with aiohttp.ClientSession() as session:
        client = RnetBridgeClient(session, "127.0.0.1", fake_bridge.port)
        snapshot = await client.connect()
        assert client.info is not None
        assert client.info.bridge_id == BRIDGE_ID
        assert client.info.proto == 1
        assert not client.info.auth_required
        assert sorted(snapshot.zones) == [1, 2, 3, 4, 5, 6]
        assert snapshot.link.up
        assert snapshot.zones[3].turn_on_volume == 20
        await client.close()


async def test_get_info(fake_bridge: FakeBridge) -> None:
    """GET /api/info."""
    async with aiohttp.ClientSession() as session:
        info = await async_get_info(session, "127.0.0.1", fake_bridge.port)
    assert info["id"] == BRIDGE_ID
    assert info["network"] == "ethernet"
    assert info["wifi_rssi"] is None


async def test_command_and_confirming_push(fake_bridge: FakeBridge) -> None:
    """A command resolves on its result; zone_state follows."""
    async with aiohttp.ClientSession() as session:
        client = RnetBridgeClient(session, "127.0.0.1", fake_bridge.port)
        await client.connect()
        task, queue = await _listen(client)
        await client.send_command("set_volume", 2, 30)
        msg = await asyncio.wait_for(queue.get(), 2)
        assert isinstance(msg, ZoneState)
        assert msg.zone == 2
        assert msg.volume == 30
        assert fake_bridge.commands[-1] == {
            "type": "command",
            "id": 1,
            "cmd": "set_volume",
            "zone": 2,
            "value": 30,
        }
        await _stop(task)


@pytest.mark.parametrize(
    ("cmd", "zone", "value", "error"),
    [
        ("set_volume", 1, 51, "invalid"),
        ("set_power", 9, True, "invalid"),
        ("set_power", 1, 1, "invalid"),  # must be a JSON bool
        ("warp", 1, None, "invalid"),
    ],
)
async def test_command_errors(
    fake_bridge: FakeBridge, cmd: str, zone: int, value: bool | int | None, error: str
) -> None:
    """Rejected commands raise with the bridge's error name."""
    async with aiohttp.ClientSession() as session:
        client = RnetBridgeClient(session, "127.0.0.1", fake_bridge.port)
        await client.connect()
        task, _ = await _listen(client)
        with pytest.raises(RnetBridgeCommandError) as err:
            await client.send_command(cmd, zone, value)
        assert err.value.error == error
        await _stop(task)


async def test_link_down_command_and_push(fake_bridge: FakeBridge) -> None:
    """link pushes arrive, and commands fail with link_down."""
    async with aiohttp.ClientSession() as session:
        client = RnetBridgeClient(session, "127.0.0.1", fake_bridge.port)
        await client.connect()
        task, queue = await _listen(client)
        await fake_bridge.set_link(False)
        msg = await asyncio.wait_for(queue.get(), 2)
        assert isinstance(msg, LinkState)
        assert not msg.up
        with pytest.raises(RnetBridgeCommandError) as err:
            await client.send_command("set_power", 1, True)
        assert err.value.error == "link_down"
        await _stop(task)


async def test_auth(socket_enabled: None) -> None:
    """Token required: none and wrong fail, right succeeds."""
    bridge = FakeBridge(token="s3cret")
    port = await bridge.start()
    try:
        async with aiohttp.ClientSession() as session:
            with pytest.raises(RnetBridgeAuthError):
                await RnetBridgeClient(session, "127.0.0.1", port).connect()
            with pytest.raises(RnetBridgeAuthError):
                await RnetBridgeClient(session, "127.0.0.1", port, "wrong").connect()
            client = RnetBridgeClient(session, "127.0.0.1", port, "s3cret")
            await client.connect()
            assert client.info is not None
            assert client.info.auth_required
            await client.close()
    finally:
        await bridge.stop()


async def test_unsupported_proto(socket_enabled: None) -> None:
    """A different proto is refused, and reported."""
    bridge = FakeBridge(proto=2)
    port = await bridge.start()
    try:
        async with aiohttp.ClientSession() as session:
            with pytest.raises(RnetBridgeProtocolError) as err:
                await RnetBridgeClient(session, "127.0.0.1", port).connect()
        assert err.value.proto == 2
    finally:
        await bridge.stop()


async def test_cannot_connect(socket_enabled: None) -> None:
    """Nothing listening: a connection error."""
    bridge = FakeBridge()
    port = await bridge.start()
    await bridge.stop()  # the port is now closed
    async with aiohttp.ClientSession() as session:
        with pytest.raises(RnetBridgeConnectionError):
            await RnetBridgeClient(session, "127.0.0.1", port).connect()
        with pytest.raises(RnetBridgeConnectionError):
            await async_get_info(session, "127.0.0.1", port)


async def test_drop_ends_listen_and_fails_pending(fake_bridge: FakeBridge) -> None:
    """When the bridge drops the connection, listen() raises."""
    async with aiohttp.ClientSession() as session:
        client = RnetBridgeClient(session, "127.0.0.1", fake_bridge.port)
        await client.connect()
        task, _ = await _listen(client)
        await asyncio.sleep(0)
        await fake_bridge.drop_clients()
        with pytest.raises(RnetBridgeConnectionError):
            await asyncio.wait_for(task, 2)
        assert not client.connected
        with pytest.raises(RnetBridgeConnectionError):
            await client.send_command("refresh")
