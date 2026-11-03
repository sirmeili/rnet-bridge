# Progress

## Done
- M1 codec merged (PR #2); enclosure merged (PR #3); board switch to ESP32-POE-ISO merged (PR #4).
- 2026-11-01: Windows dev machine set up. `pio test -e native` passes; `pio run -e esp32` succeeds.

## In progress
- `feat/m2-first-contact`: M2 serial console, diagnostics, and the bring-up checklist in `docs/rnet-notes.md`.
  Merged with `main` on 2026-11-02. Board flashed with 0.0.2 over USB (COM4, CH340).
  - M0.1 loopback: PASS (recorded in `rnet-notes.md`).
  - **M0 and the whole M2 checklist are done (2026-11-02).** All results are in `docs/rnet-notes.md`, with a summary
    table at the end. The cable works both ways, every request was answered in about 40 ms, and nothing was ever
    retransmitted. The controller was left as found: zone 1 on, source 2, volume 35, bass 0.
  - The keypad's power button is broken (hardware). It never sends `6C`.
  - Follow-ups applied 2026-11-02 with the human's OK:
    - `config::kSessionStartEvent` → `kNone`, after a clean power-cycle test.
    - `config::kSendDataAck = false`: the M3 engine sends no data ACKs and treats the controller's `02` handshake
      after our Set Data as a link-health signal (SPEC §3.5).
    - All Zones On is a disabled-by-default entity (SPEC §5.4), so users with a controller that supports it can
      enable it.
    - SPEC §3.3–§3.9 and §11 now record the answers. `duplicate_replies` counts only replies to our own requests
      (SPEC §3.5); the M2 console's detector also flags repeated keypad frames, which is fine for bring-up.
    - Nothing in the current firmware reads the two constants yet; the M3 engine will.
  - Branch not pushed yet: waiting on the human to confirm the other computer has no unpushed work.

## Next
- M0 and M2 need the human and the hardware (SPEC §9). M5 onward can proceed without hardware using `fake_bridge.py`.

## Decisions
- 2026-11-01: Run integration tests in Docker on this Windows machine. Home Assistant imports `fcntl`, so
  `pytest-homeassistant-custom-component` cannot load on native Windows. Python 3.14 is required.
- 2026-11-01: Install PlatformIO 6.2.0 to match the pin in `ci.yml`.
- 2026-11-02: The hardware is an Olimex ESP32-POE-ISO + MOD-RS232 on the UEXT connector, and the network is Ethernet
  with Wi-Fi fallback (both human decisions). UART1 on GPIO4 TX / GPIO36 RX, verified against the Rev O schematic
  on `feat/m2-first-contact`. A male-to-male null-modem cable is required (SPEC §2.3).

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

## Open questions
- The fallback rules in SPEC §4.4 (10 s Ethernet wait, prefer Ethernet when it returns, when the portal starts) are
  a proposal. Confirm them before M4.
- SPEC §11 item 11: `wifi_rssi` in `/api/info` on Ethernet (M4).
- Arduino core version for Ethernet: Olimex's example targets core 3.x, but the pinned platform has core 2.0.17.
  Decide at M4.
- Does the other dev computer have unpushed work on `feat/m2-first-contact`? Check before pushing this merge.
