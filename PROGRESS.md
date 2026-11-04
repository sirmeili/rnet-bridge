# Progress

## Done
- M1 codec (PR #2); enclosure (PR #3, then the lit button and status LED in PR #9); board switch to the
  ESP32-POE-ISO (PR #4).
- 2026-11-01: Windows dev machine set up.
- 2026-11-02: M0 + M2 (PR #5). SPEC §11 items 1–10 answered on the CAA6.6 (`docs/rnet-notes.md`).
- 2026-11-02: M3 engine (PR #6), verified on hardware.
- 2026-11-03: M4 network (PR #7), verified on hardware. Known issues: Ethernet drops while the forced portal's AP
  runs; WiFiManager blocks ~0.5 s at portal start.
- 2026-11-03: M5 integration skeleton (PR #8) and M6 entities and options flow (PR #10).
- 2026-11-04: M7 polish (PR #11). **v0.1.0 released**: firmware.bin, firmware-factory.bin, rnet_bridge.zip. The
  human flashed it over OTA and installed the integration through HACS on the real HA.

## In progress
- `fix/turn-on-volume-keeps-volume`, for release v0.1.1. The CAA6.6 sets a playing zone's volume to a new turn-on
  volume; the engine now restores the volume in the next frame. Verified on hardware: zone 1 stayed at 34, and the
  human heard no change with music playing. Versions bumped to 0.1.1. Next: PR, merge, release v0.1.1, then OTA
  the bridge.

## Next
- Option B for turn-on volume ("wait until the zone is off"), as a setting: plan in
  `docs/plans/turn-on-volume-defer.md`. Waiting on two human decisions (default mode; integration vs firmware).
- M8 hardening (SPEC §9): 48 h soak; Ethernet/Wi-Fi drop and controller power-cycle recovery; HA restart; OTA
  from the HA host. Also test the API token and OTA password on hardware (never set so far).
- Firmware for the enclosure's WS2812 status LED: confirm wiring and voltage first. Not in SPEC yet; ask the
  human when to schedule it.

## Decisions
- 2026-11-01: Integration tests run in Docker on this Windows machine (HA imports `fcntl`). PlatformIO 6.2.0, to
  match `ci.yml`.
- 2026-11-02 (human): hardware is an Olimex ESP32-POE-ISO + MOD-RS232 on UEXT; network is Ethernet with Wi-Fi
  fallback. UART1 GPIO4 TX / GPIO36 RX; a male-to-male null-modem cable.
- 2026-11-02 (human): keep the M2 console as the `esp32-console` build env.
- 2026-11-02 (human): a command's result is `ok` once sent, and the poll confirms it. A volume step while muted
  steps from the stored volume and unmutes. All Zones On is disabled by default. No data ACKs and no session
  start (verified).
- 2026-11-03 (human): the SPEC §4.4 fallback rules; Arduino core stays 2.0.17; `/api/info` gains `network`.
  `aiohttp` approved for `tools/`.
- 2026-11-03: HA compatibility from 2026.3 to current (`probatio`/`voluptuous`, `via_device_id`/`via_device`,
  `suggested_area` only before 2026.9). CI tests both ends. Details in SPEC §5.
- 2026-11-04 (human): keep the placeholder speaker brand icon for now.
- 2026-11-04 (human): fix the turn-on volume side effect with option A (restore the volume right after) rather
  than B (defer until the zone is off). The restore was inaudible.

## Open questions
- Board revision printed on the PCB: not yet recorded in `docs/rnet-notes.md`.

## Dev environment (Windows)
- PlatformIO: `uv tool install platformio==6.2.0` (on PATH as `pio`).
- Host compiler for native tests: WinLibs g++ via winget. It's on the user PATH; open a new terminal after
  installing.
- ruff and mypy: `.venv` (Python 3.14, gitignored). Its `pytest` does not work (see Decisions).
- pytest, in Docker. Volumes: `rnet-bridge-venv` (current HA) and `rnet-bridge-venv-min` (HA 2026.3.1). Use image
  `python:3.14`, which has a compiler. From the repo root in Git Bash:
  ```bash
  MSYS_NO_PATHCONV=1 docker run --rm -v rnet-bridge-venv:/venv -v "$(pwd -W):/src" -w /src python:3.14 /venv/bin/pytest tests/
  ```
- OpenSCAD 2021.01: `C:\Program Files\OpenSCAD\openscad.exe`, not on PATH.
- The board's USB serial port has been COM4 and COM5 on this PC (CH340); check `pio device list`.
- The bridge is on Ethernet at 10.0.0.165 (`rnet-bridge-083b.local`).
