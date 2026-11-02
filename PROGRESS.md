# Progress

## Done
- M1 codec merged (PR #2); enclosure merged (PR #3).
- 2026-11-01: Windows dev machine set up. `pio test -e native` passes 41/41; `pio run -e esp32` succeeds.

## In progress
- Nothing.

## Next
- M0 and M2 need the human and the hardware (SPEC §9). M5 onward can proceed without hardware using `fake_bridge.py`.

## Decisions
- 2026-11-01: Run integration tests in Docker on this Windows machine. Home Assistant imports `fcntl`, so
  `pytest-homeassistant-custom-component` cannot load on native Windows. Python 3.14 is required.
- 2026-11-01: Install PlatformIO 6.2.0 to match the pin in `ci.yml`.

## Dev environment (Windows)
- PlatformIO: `uv tool install platformio==6.2.0` (on PATH as `pio`).
- Host compiler for native tests: WinLibs g++ via winget.
- ruff and mypy: `.venv` (Python 3.14, gitignored). Its `pytest` does not work; see Decisions.
- pytest, in Docker with the test environment in the `rnet-bridge-venv` volume. From the repo root in Git Bash:
  ```bash
  MSYS_NO_PATHCONV=1 docker run --rm -v rnet-bridge-venv:/venv -v "$(pwd -W):/src" -w /src python:3.14-slim /venv/bin/pytest tests/
  ```
  So far this environment has only run a smoke test outside the repo, because `tests/` does not exist yet.
- OpenSCAD 2021.01: `C:\Program Files\OpenSCAD\openscad.exe`, not on PATH.

- 2026-11-02: The target board is the Olimex ESP32-POE-ISO + MOD-RS232 (human decision), not the D1 Mini +
  MAX3232 in SPEC §2. SPEC, CLAUDE.md, `platformio.ini` (board `esp32-poe-iso`) and `config.h` pins still need
  updating to match.

## Open questions
- Network for the POE-ISO: Ethernet only, Ethernet with Wi-Fi fallback, or Wi-Fi only? SPEC §1.2 lists Ethernet/PoE
  as a non-goal, and §4.4 assumes a WiFiManager portal.
- UART pins: the current `config.h` uses GPIO16/17. On the POE-ISO, GPIO17 is believed to be the Ethernet PHY clock,
  and the MOD-RS232 is believed to connect through UEXT on GPIO4 (TX) / GPIO36 (RX). Confirm against the Olimex
  schematic and the actual wiring before changing anything.
