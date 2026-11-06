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
- `feat/m8-hardening` (M8, SPEC §9):
  - Done: `tools/soak_monitor.py` + `tools/soak/Dockerfile` (the human runs the 48 h soak on a Debian Docker host,
    since this PC can't stay on); measurable soak pass criteria in SPEC §8 (human-approved); the fake bridge now
    pushes zone_state only on change and grows its frame counters, like the firmware; the M8 procedure in
    `docs/rnet-notes.md`. Pre-check against the real bridge: all criteria passed.
  - 2026-11-05: recovery tests R1–R7 all pass (`docs/rnet-notes.md`). Fixes found on the way, all on this branch:
    late answers to retried requests no longer count as duplicates; the library no longer closes the oldest
    WebSocket client when over 4; clients are closed before any restart (HA noticed a reboot after 61–75 s, now
    <1 s); no mDNS while the portal is open; the integration finds a moved bridge over mDNS and has a reconfigure
    step. The soak monitor records WebSocket close codes.
  - On the bridge: firmware up to 8f0b4a1 (close-before-restart). **Not yet on hardware:** the mDNS-in-portal fix
    (0bcf38a) and the integration change (3c1ead6, needs installing on the real HA).
  - Checks: native 140/140; pytest 65/65 on current HA and 2026.3.1; ruff and mypy clean; firmware builds.
  - Bridge now has a test API token and OTA password set (values given by the human for testing; not stored in
    the repo). The soak monitor needs `-e BRIDGE_TOKEN=...`.
  - Next: verify the two fixes on hardware (portal: HA keeps the Ethernet address), then the 48 h soak on the
    Debian host, then results and the PR. Watch the dropped-frame rate (5 in ~75 min after the controller
    power-cycle; likely line noise, no missed replies).
- v0.1.1 released 2026-11-04 (turn-on volume restore); the bridge runs 0.1.1.

## Next
- Option B for turn-on volume ("wait until the zone is off"), as a setting: plan in
  `docs/plans/turn-on-volume-defer.md`. Default mode decided (defer). **Blocked: where it runs (integration or
  firmware) is undecided; ask the human before starting.**
- M8: the 48 h soak (recovery tests done).
- Possible later: the bridge's Wi-Fi link dropped HA's connection 6 times in its first 4 minutes at -83 dBm
  (R2). Wi-Fi is only the fallback; not pursued.
- Firmware for the enclosure's WS2812 status LED: confirm wiring and voltage first. Not in SPEC yet; ask the
  human when to schedule it.

## Decisions
- 2026-11-04 (human): run the 48 h soak in Docker on a Debian x86 server on the bridge's subnet, built locally
  (no published image). Soak pass criteria as in SPEC §8. OK to set an API token and OTA password for testing.
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
- 2026-11-04 (human): plan option B as a setting, alongside A. Its default mode will be "wait until the zone is
  off". Where it runs is still open (see Open questions).

- 2026-11-05 (human): fix the stale address after the portal in both the integration (mDNS lookup + reconfigure)
  and the firmware (no mDNS during the portal).

## Open questions
- Option B: run the "wait until off" logic in the integration (recommended) or in the firmware? The human is
  deciding; required before that work starts (`docs/plans/turn-on-volume-defer.md`).
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
