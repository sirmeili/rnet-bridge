# CLAUDE.md

## What this repo is
RNET Bridge: an ESP32 + MAX3232 device that plugs into a Russound RNET controller's RS-232 port (target:
CAA6.6). It exposes the controller over a WebSocket/JSON API, alongside a Home Assistant custom integration
(`rnet_bridge`) that turns each zone into a media player plus tone/config entities.

**Read `docs/SPEC.md` before doing anything substantial.** It is the source of truth for scope, the RNET
implementation details, the network API, the entity model, and the milestone plan (§9). Work milestone by
milestone, and do not jump ahead to later milestones unless asked.

## Layout
- `firmware/` — PlatformIO, Arduino framework, board `esp32-poe-iso`
  - `lib/rnet/` — pure C++ protocol code (codec, messages, engine, zone model). **No Arduino headers here.**
- `custom_components/rnet_bridge/` — HA integration (Python, asyncio)
- `tools/` — `fake_bridge.py` (simulated bridge), `rnet_decode.py`, `rnet_cli.py`
- `tests/` — pytest for the integration
- `docs/SPEC.md` — project specification ("SPEC §x" in this file refers to its sections)
- `docs/protocol.md` — WebSocket/HTTP API contract between firmware and integration
- `docs/rnet-notes.md` — findings verified on real hardware (overrides the PDF)
- `docs/reference/russound-rnet-protocol.pdf` — Russound's protocol doc (contains errors; see SPEC §3.9)
- `docs/reference/russound-rnet-protocol.md` — editable Markdown transcription of the PDF, with errata callouts (Appendix A)

## Commands
```bash
# Firmware
cd firmware
pio run -e esp32                     # build
pio run -e esp32 -t upload           # flash over USB
pio device monitor -b 115200         # USB serial console
pio test -e native                   # host unit tests (codec/engine)

# Integration
pip install -r requirements_test.txt
pytest tests/
ruff check . && ruff format --check .
mypy custom_components/rnet_bridge
# HACS + hassfest validation runs in GitHub Actions (validate.yml); check it after pushing

# Dev without hardware
python tools/fake_bridge.py --port 8080
```

## Golden rules
1. **`docs/protocol.md` is a contract.** Any change to the WebSocket/HTTP API updates the firmware, the
   integration, `fake_bridge.py`, and the doc in the same change. Breaking changes bump `proto`.
2. **Trust hardware over the PDF.** The Russound PDF has known errors (SPEC §3.9). Hardware-verified facts go in
   `docs/rnet-notes.md`. Never "fix" a test vector in SPEC §3.10 to make code pass. Fix the code.
3. **Don't guess unresolved protocol details** (SPEC §11, especially the ACK format). Make them a single named
   constant, and ask the human to verify on hardware. Claude cannot reach the device.
4. **Firmware stays config-free** apart from network settings. Zone and source names and enablement live only in the HA integration.
5. **Never block in the firmware main loop.** No `delay()` in protocol paths. State machines plus `millis()`.
6. **The integration does no optimistic state.** Entities reflect `zone_state` pushed by the bridge.
7. **Keep the repo HACS-valid at all times** (SPEC §5.6): only one folder under `custom_components/`, every runtime file
   inside it, no external `requirements`, `hacs.json` at the root, manifest keys complete and ordered, and brand icons present.
   Any version bump changes `manifest.json` `version`, and release tags must match it exactly (`v0.1.0` ↔ `0.1.0`).
   Never use Russound's logo or trademarks in brand assets.
8. API zone/source numbers are **1-based**. RNET wire values are **0-based**. Convert only at the firmware's message layer.

## Conventions
- C++: C++17, `snake_case` files, classes `PascalCase`, no exceptions, avoid heap allocation in the RX/TX path,
  fixed-size buffers with bounds checks. Every protocol builder/parser gets a native unit test.
- Python: follow Home Assistant developer guidelines — fully async, type hints everywhere, `has_entity_name`,
  translated entity names and errors, `entity_category` for config/diagnostic entities, no blocking I/O.
  Ruff for lint/format.
- Keep `docs/SPEC.md` up to date when a decision changes. Note the change and the reason.
- Small, focused commits. Conventional commit prefixes (`feat(firmware):`, `fix(integration):`, `docs:`, `test:`).

## Git workflow (mandatory)
- **Never commit to, push to, merge into, or rebase `main` directly.** All changes reach `main` only through a pull request.
- **Before starting any new work, create a new branch from an up-to-date `main`.** Check `git branch --show-current` first.
  If you are on `main`, run `git switch main && git pull && git switch -c <branch>` before editing anything.
- **One branch per major piece of work.** Group related changes on a single branch instead of making many tiny branches.
  A milestone from SPEC §9 is the default unit of work.
  - Branch names: `<type>/<short-description>`, e.g. `feat/m1-rnet-codec`, `feat/m5-integration-skeleton`,
    `fix/ack-handshake`, `docs/rnet-notes`.
  - Small follow-ups that belong to work still open on an existing branch go on that branch, not a new one.
- When the work is ready, push the branch and open a PR to `main` (`gh pr create`). The PR description includes:
  a summary, the SPEC milestone/sections addressed, test results, and any hardware verification still required from the human.
- Never merge your own PR, force-push to `main`, or delete `main`'s history. The human reviews and merges.
- If you notice uncommitted changes on `main`, stop and ask the human how to handle them. Don't commit them there.

## Hardware notes (for context)
- Board: Olimex ESP32-POE-ISO (WROOM, 4 MB), PlatformIO `esp32-poe-iso`, CH340 USB-serial. RS-232 via Olimex
  MOD-RS232 on the UEXT connector (SPEC §2).
- UART1: GPIO4 TX, GPIO36 RX (input-only) → MOD-RS232 (MAX3232, 3.3 V). 19200 8N1, no flow control.
  GPIO16/17 are not available (GPIO17 is the Ethernet clock).
- Controller DB9: pin 2 TX, pin 3 RX, pin 5 GND. Both it and the MOD-RS232 transmit on pin 2, so a male-to-male
  null-modem cable is required (SPEC §2.3).
- The controller's port supports one client only. The old Moxa/`russound_rnet` setup must be disconnected.
