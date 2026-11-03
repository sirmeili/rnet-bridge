# Progress

## Done
- M1 codec merged (PR #2); enclosure merged (PR #3); board switch to ESP32-POE-ISO merged (PR #4).
- 2026-11-01: Windows dev machine set up. `pio test -e native` passes; `pio run -e esp32` succeeds.
- 2026-11-02: M0 and M2 merged (PR #5). Every SPEC §11 bring-up question (items 1–10) answered on the CAA6.6;
  results in `docs/rnet-notes.md`. `kSessionStartEvent = kNone`, `kSendDataAck = false`, and All Zones On is
  disabled by default.

## In progress
- `feat/m3-engine` (M3, SPEC §9):
  - Done: `zone_model` (18 native tests), `rnet_engine` (28 native tests against a scripted fake controller), the
    `esp32` env running the engine with a USB state dump and commands (`usb_cli`), and the M2 console moved to its
    own `esp32-console` env (also built in CI).
  - Checks so far: `pio test -e native` 100/100, and `pio run -e esp32` / `-e esp32-console` both succeed with no
    warnings.
  - **Hardware check passed (2026-11-02)**, recorded in `docs/rnet-notes.md` ("M3 engine check"). Keypad changes
    reached the state dump within one poll cycle, and every USB command was confirmed by the priority poll about
    190 ms later. Zero checksum errors, timeouts or retries. Also found: an off zone's reported volume equals its
    turn-on volume (SPEC §3.4).
  - Board left with the engine firmware, zone 1 on (source 2, volume 35), everything else as found.
  - Next: PR review and merge by the human.

## Next
- M4 network (needs the open questions below settled). M5 onward can proceed without hardware using `fake_bridge.py`.

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
- The fallback rules in SPEC §4.4 (10 s Ethernet wait, prefer Ethernet when it returns, when the portal starts) are
  a proposal. Confirm them before M4.
- SPEC §11 item 11: `wifi_rssi` in `/api/info` on Ethernet (M4).
- Arduino core version for Ethernet: Olimex's example targets core 3.x, but the pinned platform has core 2.0.17.
  Decide at M4.
- Board revision printed on the PCB: not yet recorded in `docs/rnet-notes.md`.
