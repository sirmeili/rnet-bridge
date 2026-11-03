# Progress

## Done
- M1 codec merged (PR #2); enclosure merged (PR #3); board switch to ESP32-POE-ISO merged (PR #4).
- 2026-11-01: Windows dev machine set up. `pio test -e native` passes; `pio run -e esp32` succeeds.
- 2026-11-02: M0 and M2 merged (PR #5). Every SPEC §11 bring-up question (items 1–10) answered on the CAA6.6;
  results in `docs/rnet-notes.md`. `kSessionStartEvent = kNone`, `kSendDataAck = false`, and All Zones On is
  disabled by default.

## In progress
- `feat/m4-network` (M4, SPEC §9):
  - Done: `docs/protocol.md` (proto 1 contract); engine raw sends and frame tap; `lib/bridge` (network policy,
    double reset, API JSON); `src/net` (Ethernet, Wi-Fi fallback, portal, API server, mDNS, OTA, settings, log);
    `tools/rnet_cli.py`. Firmware 0.0.3 on min_spiffs partitions.
  - Checks so far: `pio test -e native` 133/133; `pio run -e esp32` (53% flash, 17% RAM) and `-e esp32-console`
    succeed with no warnings; `rnet_cli.py` passes ruff and `mypy --strict`.
  - **Hardware tests passed (2026-11-03)**, recorded in `docs/rnet-notes.md` ("M4 network check"): Ethernet/PoE,
    mDNS, HTTP, WebSocket commands, keypad push, tracing and logs, OTA, portal → Wi-Fi, Ethernet return, double
    reset, factory reset. Fixes made along the way: portal on channel 6, API paused before the portal, saved-Wi-Fi
    detection, factory reset erasing Wi-Fi, a thread-safe log, and Wi-Fi modem sleep off.
  - Known issues: Ethernet drops while the forced portal's AP runs; WiFiManager blocks about 0.5 s at portal start.
  - Not hardware-tested: API token and OTA password (none were set).
  - Board state: factory reset (no Wi-Fi saved, default settings), on Ethernet at 10.0.0.165.
  - Next: PR review and merge by the human.

## Next
- M5 integration skeleton. It can proceed without hardware using `fake_bridge.py`.

## Decisions
- 2026-11-01: Run integration tests in Docker on this Windows machine. Home Assistant imports `fcntl`, so
  `pytest-homeassistant-custom-component` cannot load on native Windows. Python 3.14 is required.
- 2026-11-01: Install PlatformIO 6.2.0 to match the pin in `ci.yml`.
- 2026-11-02: The hardware is an Olimex ESP32-POE-ISO + MOD-RS232 on the UEXT connector, and the network is Ethernet
  with Wi-Fi fallback (both human decisions). UART1 on GPIO4 TX / GPIO36 RX, verified against the Rev O schematic.
  A male-to-male null-modem cable is required (SPEC §2.3).
- 2026-11-02 (human): keep the M2 console as a separate `esp32-console` build env, for other controller models and
  later rechecks.
- 2026-11-02 (human): a command's result is `ok` once its frame is sent; the follow-up poll confirms it (SPEC §4.3).
- 2026-11-02 (human): a volume step while muted steps from the stored volume and unmutes (SPEC §4.3).
- 2026-11-03 (human): the SPEC §4.4 fallback rules as written; Arduino core stays 2.0.17; `/api/info` gains
  `network`, with `wifi_rssi` null on Ethernet. `aiohttp` approved for `tools/`.

## Dev environment (Windows)
- PlatformIO: `uv tool install platformio==6.2.0` (on PATH as `pio`).
- Host compiler for native tests: WinLibs g++ via winget. It's on the user PATH, so open a new terminal after installing.
- ruff and mypy: `.venv` (Python 3.14, gitignored). Its `pytest` does not work; see Decisions.
- pytest, in Docker with the test environment in the `rnet-bridge-venv` volume. From the repo root in Git Bash:
  ```bash
  MSYS_NO_PATHCONV=1 docker run --rm -v rnet-bridge-venv:/venv -v "$(pwd -W):/src" -w /src python:3.14-slim /venv/bin/pytest tests/
  ```
  So far this environment has only run a smoke test outside the repo, because `tests/` does not exist yet.
- OpenSCAD 2021.01: `C:\Program Files\OpenSCAD\openscad.exe`, not on PATH.
- The board shows up as COM4 (CH340) on this PC.

## Open questions
- Board revision printed on the PCB: not yet recorded in `docs/rnet-notes.md`.
