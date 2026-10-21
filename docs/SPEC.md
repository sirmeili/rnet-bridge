# RNET Bridge — Project Specification

**Status:** v1 specification
**Target hardware:** Russound CAA6.6 (should work with other RNET controllers: CAS44, CAM6.6, CAV6.6)
**Deliverables:** ESP32 firmware + Home Assistant custom integration (HACS-ready), in one monorepo

---

## 1. Overview

Russound CAA-series controllers expose an RS-232 port that speaks the RNET™ protocol. Today, the
controller reaches Home Assistant through a Moxa NPort serial-to-IP converter and the core
`russound_rnet` integration. That setup is unreliable and exposes only a subset of features
(no bass, treble, balance, loudness, or turn-on volume).

This project replaces it with two pieces:

1. **Firmware** for an ESP32 + MAX3232 module plugged directly into the controller's DB9 port. The
   firmware owns all RNET timing (framing, escaping, checksums, handshake ACKs, pacing, retries,
   polling). It keeps a live model of zone state and exposes that state over a **WebSocket/JSON API**
   on the LAN. It advertises itself over **mDNS/zeroconf**.
2. **A Home Assistant custom integration** that discovers the bridge and creates one device per zone.
   Each zone device gets a real `media_player` (power, volume, mute, source select) plus configuration
   entities for tone and turn-on volume. A parent controller device holds the global controls.

Design principle: **the firmware is generic and config-free** apart from network settings. It always
models all 6 zones and reports raw zone and source numbers. All naming, enabling of zones/sources, and
presentation live in the HA integration's config flow and options flow. No reflash is needed to rename
a zone or enable zone 5.

### 1.1 Goals (v1)
- Reliable, low-latency control and state sync for one RNET controller with up to 6 zones and 6 sources.
- Per-zone: power, source, volume (set/step), mute (emulated), bass, treble, balance, loudness, turn-on volume.
- Global: All Zones On, All Zones Off, Resync.
- State changes made at a keypad show up in HA within a few seconds.
- Zero-config discovery in HA. Wi-Fi provisioning via captive portal. OTA firmware updates.
- Users choose which zones and sources are enabled, and their names, in the HA UI.
- Integration is structured so it can be published on HACS.

### 1.2 Non-goals (v1) — candidate future work
- Pushing text to keypad displays (protocol §9).
- Source control / keypad button events to sources (play, next, etc.; protocol §6.2).
- Multiple linked controllers (protocol supports up to 6; design should not preclude it).
- Do Not Disturb, Party Mode, Background Color. Per the protocol's feature table these are not
  supported on the CAA6.6, and all-zone-info marks Party/DND as unavailable on CAS44/CAA66.
- Ethernet/PoE hardware revision.
- Getting the integration into HA core (that would require publishing the client as a PyPI library).

---

## 2. Hardware

### 2.1 Bill of materials (v1 — parts on hand)
| Part | Notes |
|---|---|
| ESP32 "D1 Mini" style board, USB-C (ESP32-WROOM) | PlatformIO board: `wemos_d1_mini32` (verify against the actual module) |
| MAX3232 RS232↔TTL module with **female** DB9 | Powered at 3.3 V |
| DB9 cable/adapter — **likely male-to-male null-modem** | See §2.3 |
| 5 V USB-C power supply | Near the controller |

### 2.2 Wiring (ESP32 ↔ MAX3232)
| ESP32 | MAX3232 module | Notes |
|---|---|---|
| 3V3 | VCC | Do **not** use 5 V; keep TTL levels at 3.3 V |
| GND | GND | |
| GPIO17 (UART2 TX) | TXD / TX (TTL input to the module) | Pins configurable in firmware build flags |
| GPIO16 (UART2 RX) | RXD / RX (TTL output from the module) | |

Module silkscreens vary in whether "TX" means "connect your TX here" or "this module's TX". If no
traffic is seen during bring-up, swap the two TTL lines. This is harmless.

### 2.3 RS-232 side (module ↔ controller)
- Controller port: female DB9. Pin 2 = controller **transmit**, pin 3 = controller **receive**,
  pin 5 = ground (protocol doc §3.2; the doc's "DB-96" is a typo). This is DCE-style wiring, which
  is why a straight cable to the Moxa (a male DTE device) works today.
- The MAX3232 module is also female and usually DCE-wired, so a **male-to-male null-modem** (2↔3
  crossed, 5↔5) is expected.
- **Verify before buying:** with each device powered, measure DB9 pins 2 and 3 against pin 5. The
  transmit pin idles at roughly −5 to −9 V, and the receive pin reads near 0 V. Wire each transmit pin
  to the other device's receive pin.
- Only one device can own the controller's RS-232 port, so the Moxa must be disconnected. Remove or
  disable the existing `russound_rnet` integration in HA as well.

### 2.4 Serial settings
19200 baud, 8 data bits, no parity, 1 stop bit, no flow control.

---

## 3. RNET protocol — what the firmware implements

The reference PDF is `docs/reference/russound-rnet-protocol.pdf` (RNET protocol doc v1.00.04). A Markdown
transcription with errata callouts is at `docs/reference/russound-rnet-protocol.md`, and that is the version
to edit and annotate. This section is the authoritative implementation summary, and **corrects known errors
in the PDF** (see §3.9).
All hex values; `zz` = zone − 1 (0–5), `ss` = source − 1 (0–5).

### 3.1 Framing
```
F0 | target(ctrl, zone, keypad) | source(ctrl, zone, keypad) | type | body... | checksum | F7
```
- `0xF0` start, `0xF7` end. All bytes between them have MSB = 0, except the invert character.
- **Escaping:** any body byte > 0x7F is transmitted as `F1` followed by the bitwise-inverted byte
  (`~b & 0xFF`, which yields a value ≤ 0x7F). On receive, drop `F1` and invert the next byte.
- **Checksum:** sum every byte **as transmitted** from `F0` through the last body byte (including any
  `F1` and the escaped byte), add the count of those bytes, then `& 0x7F`. Checksum and `F7` are excluded.
- Message types: `00` Set Data, `01` Request Data, `02` Handshake, `05` Event.

### 3.2 Device IDs
- Controller: `00 00 7F` (controller 1, "the controller itself").
- Bridge (us): source `00 zz 70` — controller 0, zone field = the zone being addressed, keypad `0x70`.
  This follows the doc's templates and hex tables. Replies come back targeted to `00 zz 70`.
- All controllers: target controller byte `7E` (used by All On/Off).

### 3.3 Command templates (bytes before checksum)
| Operation | Frame (before checksum + F7) |
|---|---|
| Zone power on/off | `F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 pp 00 zz 00 01` (pp: 00 off, 01 on; source zone byte is `00`, see note) |
| All zones on/off | `F0 7E 00 7F 00 00 70 05 02 02 00 00 F1 22 00 pp 00 00 00 01` |
| Select source | `F0 00 00 7F 00 zz 70 05 02 00 00 00 F1 3E 00 00 00 ss 00 01` (note byte 10 = `00`, unlike the others) |
| Set volume | `F0 00 00 7F 00 zz 70 05 02 02 00 00 F1 21 00 vv 00 zz 00 01` (vv 0x00–0x32) |
| Keypad event (e.g. vol up 7F / vol down 80) | `F0 00 00 7F 00 zz 70 05 02 02 00 00 ee 00 00 00 00 00 01` (`80` must be escaped as `F1 7F`) |
| Set zone param | `F0 00 00 7F 00 zz 70 00 05 02 00 zz 00 pp 00 00 00 01 00 01 00 vv` |
| Get zone param | `F0 00 00 7F 00 zz 70 01 05 02 00 zz 00 pp 00 00` |
| Get all zone info | `F0 00 00 7F 00 zz 70 01 04 02 00 zz 07 00 00` |

**Zone power source zone byte (decision 2026-10-21).** Zone On/Off sends source `00 00 70` (byte 6 = `00`) for
every zone. The zone is carried in byte 18 only. This matches the PDF §13 hex table verbatim. The PDF's §8.1.1
template puts `zz` in byte 6 instead. The §3.10 "Zone 2 On" vector previously mixed the two forms (byte 6 =
`01` with the table's checksum `13`), and has been corrected to the table's frame. Whether `zz` is also
accepted is checked in M2 (§11 item 8). Every other zone command keeps `zz` in byte 6, and Source Select needs
it, because byte 6 is its only zone field.

Zone param ids `pp`: `00` bass, `01` treble, `02` loudness, `03` balance, `04` turn-on volume.
Value encodings:
- Volume and turn-on volume: 0x00–0x32 (0–50; keypad shows ×2, i.e. 0–100).
- Bass, treble, balance: 0x00–0x14 (0x0A = flat/center). Bass/treble: value − 10 = dB step. Balance: 0x00 = full left.
- Loudness: 0/1.

Prefer the discrete **Set Volume** event over Volume Up/Down keypad events. Implement volume step as
read-modify-write of the cached volume.

### 3.4 Replies
**All zone info** (reply to `Get all zone info`), after unescaping:
```
F0 00 zz 70 00 00 7F 00 00 04 02 00 zz 07 00 00 01 00 0C 00 [d0..d10] 00 cs F7
```
1-based bytes 21–31 (`d0..d10`):

| Byte | Field |
|---|---|
| 21 | power (0/1) |
| 22 | source − 1 |
| 23 | volume 0–50 |
| 24 | bass 0–20 |
| 25 | treble 0–20 |
| 26 | loudness 0/1 |
| 27 | balance 0–20 |
| 28 | system on (any zone on) |
| 29 | shared source |
| 30 | party mode (N/A on CAA6.6 — ignore) |
| 31 | DND (N/A on CAA6.6 — ignore) |

Turn-on volume is **not** in all-zone-info. Fetch it with `Get zone param pp=04`. The expected reply is
`F0 00 zz 70 00 00 7F 00 00 05 02 00 zz 00 04 00 00 01 00 01 00 vv cs F7`.

**Parse replies by message type + path bytes, never by fixed total length.** Handle unknown frames by
logging and discarding them.

### 3.5 Handshake / ACK — MUST VERIFY ON HARDWARE
Every Set Data reply the controller sends in response to our Request **must be acknowledged**.
Otherwise the controller retransmits and the bus stalls about 2.5 s.
- The doc shows only an *event* handshake example: `F0 00 00 60 00 7D 00 02 06 5E F7`
  (target = the device being acknowledged, source = acknowledger, type `02`, handshake type `06` = event).
- **Working hypothesis** for the data handshake: `F0 00 00 7F 00 zz 70 02 02 cs F7`
  (target = controller, source = us, type `02`, handshake type `02`).
- The handshake-type byte must be a single constant that is easy to change. Bring-up (§9, M2) determines
  the correct value. The test: if a reply arrives again with identical content about 2.5 s later, the
  ACK was not accepted. Track this as `duplicate_replies` in stats.
- The controller sends us ACKs after our Set Data messages. Accept and ignore them, but use them as
  link-health signals.

### 3.6 Session start
The doc says at least one Event must be sent before zone-specific Get/Set works. On boot and after any
RNET link loss, send the **Device Restart event**:
`F0 00 00 7F 00 00 70 05 02 02 00 00 55 01 03 00 35 00 01 cs F7`
(the doc specifies keypad `71` for ACA-E5; use `70` here). If the CAA6.6 ignores it, fall back to a
harmless event (to be decided in bring-up). Verify on hardware.

### 3.7 Timing
- One outstanding request at a time. Wait for its reply (default timeout 500 ms), send the ACK, then proceed.
- Minimum inter-frame gap: 50 ms (configurable; the doc suggests about 100 ms when not handshaking).
- Retries: up to 2 for requests. Commands (events/sets) are not retried blindly. Correctness comes from the follow-up poll.
- Link considered **down** after 5 consecutive timeouts. On recovery, resend the session-start event.

### 3.8 Polling strategy
- Round-robin `Get all zone info` across **all 6 zones**. Default: one zone every 500 ms, so a full cycle is about 3 s.
  The firmware does not know which zones the user has enabled, so it always polls all six. This is cheap.
- After any command to zone N, schedule an **immediate priority poll** of zone N about 150 ms later, so HA
  sees confirmed state quickly. Commands jump ahead of routine polls in the queue.
- Turn-on volume: poll on session start, after a set, and every 60 s per zone.
- Only publish `zone_state` to clients when something changed. Also publish a full snapshot on client connect.
- During bring-up, check whether the controller emits any unsolicited messages on keypad changes. If it
  does, document them. They are not required for v1.

### 3.9 Known errors in the PDF (do not copy these)
The full list, with evidence, is in Appendix A of `docs/reference/russound-rnet-protocol.md`. The ones that
matter for implementation:
- §6.2 "Zone 1, Menu" breakdown lists checksum `7B`. The correct value is `4E`, as shown in the example line.
- §6.2 "Zone 3, Record" example prints checksum `49`. The correct value is `4F`.
- §8.8.3 Get Turn On Volume reply template shows path `04 02 00 zz 01 00 ...` (copied from Volume). The
  expected path is `05 02 00 zz 00 04`. Verify on hardware.
- §8.8.1 describes Plus/Minus for turn-on volume as "More Left/More Right" (copy-paste error).
- §8.3.2 Get Volume reply says "Volume Level -1". This is likely a copy-paste from Get Source. Volume is raw
  0–50 everywhere else.
- §3.1 omits the data bits. The port is 8N1.
- The §13 hex table's Zone Vol Down rows render ambiguously. The correct Zone 1 frame is in §3.10 below.
- §8.1.1 (Zone On/Off template) puts the zone in the Source Zone ID (byte 6), but the §13 hex table sends
  `00` there for every zone. Both have valid checksums. See §11.
- The Quick Reference table marks **Set All Zones On as unsupported on the CAA66** (All Off is supported).
- (Removed 2026-10-21: an earlier entry said §3.2 reads "DB-96". The PDF reads "Female DB-9". The "6" came
  from text extraction.)

### 3.10 Codec test vectors (verified by hand from the PDF)
Each must encode to exactly these bytes, and decode back:
```
Zone 1 Volume Up    F0 00 00 7F 00 00 70 05 02 02 00 00 7F 00 00 00 00 00 01 7B F7
Zone 1 Volume Down  F0 00 00 7F 00 00 70 05 02 02 00 00 F1 7F 00 00 00 00 00 01 6D F7
Zone 1 On           F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 01 00 00 00 01 12 F7
Zone 1 Off          F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 00 00 00 00 01 11 F7
Zone 2 On           F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 01 00 01 00 01 13 F7
All On              F0 7E 00 7F 00 00 70 05 02 02 00 00 F1 22 00 01 00 00 00 01 0F F7
All Off             F0 7E 00 7F 00 00 70 05 02 02 00 00 F1 22 00 00 00 00 00 01 0E F7
Zone 1 Source 1     F0 00 00 7F 00 00 70 05 02 00 00 00 F1 3E 00 00 00 00 00 01 2A F7
Zone 6 Source 6     F0 00 00 7F 00 05 70 05 02 00 00 00 F1 3E 00 00 00 05 00 01 34 F7
Zone 1 Menu (remote key)  F0 00 00 7F 00 00 70 05 02 02 00 00 F1 40 00 00 00 20 00 01 4E F7
Event handshake (doc)     F0 00 00 60 00 7D 00 02 06 5E F7
Checksum example (doc)    F0 00 67 7C F1 0F 59 F7
```

---

## 4. Firmware

### 4.1 Platform
- PlatformIO, `espressif32` platform, **Arduino framework**, board `wemos_d1_mini32`.
- Libraries (pin versions in `platformio.ini`):
  - `ESP32Async/ESPAsyncWebServer` + `ESP32Async/AsyncTCP` (maintained forks) — HTTP + WebSocket
  - `bblanchon/ArduinoJson` v7
  - `tzapu/WiFiManager` — captive-portal provisioning
  - Built-in: `ESPmDNS`, `Preferences` (NVS), `Update` (OTA), `ArduinoOTA` (dev convenience)
- Environments: `esp32` (device) and `native` (host unit tests with Unity, for the codec and engine logic).

### 4.2 Module layout (`firmware/`)
```
firmware/
  platformio.ini
  include/config.h            # pins, timings, constants (handshake type, keypad id)
  src/main.cpp                # setup/loop wiring only
  lib/rnet/                   # PURE C++, no Arduino deps -> testable on native
    rnet_codec.{h,cpp}        # escape/unescape, checksum, frame builder, frame parser (byte-at-a-time state machine)
    rnet_messages.{h,cpp}     # builders for each template in §3.3, reply parsers for §3.4
    rnet_engine.{h,cpp}       # queue, single-outstanding request, timeouts, ACKs, polling scheduler, link state
    zone_model.{h,cpp}        # 6-zone state cache, change detection, mute emulation
  src/net/
    wifi_setup.{h,cpp}        # WiFiManager portal, hostname, double-reset detection
    api_server.{h,cpp}        # WebSocket + HTTP endpoints, JSON (de)serialization
    mdns.{h,cpp}
    ota.{h,cpp}
  src/platform/
    serial_port.{h,cpp}       # adapter between HardwareSerial and the engine's byte I/O interface
    log.{h,cpp}               # ring buffer + level filtering, mirrored to USB serial and WS subscribers
  test/                       # Unity tests run with `pio test -e native`
```
The engine takes injected interfaces for byte I/O and a millisecond clock, so it can be tested on the
host with a scripted fake controller.

### 4.3 Runtime model
- Non-blocking cooperative loop: no `delay()` in the main path. The engine runs as a state machine ticked from `loop()`.
- The UART RX is drained every tick into the codec's streaming parser. It resyncs on `F0` and drops partial
  frames on timeout or a bad checksum (increment `checksum_errors`).
- Commands arriving from WebSocket clients go onto a bounded queue (depth 32). If full, reply with error `busy`.
- **Mute emulation** (in `zone_model`): `mute=true` stores the current volume and sets volume to 0. `mute=false`
  restores it (or 10 if the stored value was 0). If a poll shows volume ≠ 0 while muted, clear mute
  (someone changed volume at the keypad). Mute state is RAM-only. It resets on reboot, which is acceptable.
- Volume up/down commands are applied as set-volume ±1 (raw units) on the cached value, clamped to 0–50.

### 4.4 Provisioning & settings
- First boot, or no saved credentials: start an AP `RNET-Bridge-XXXX` (last 4 of MAC) with a captive portal.
  Fields: Wi-Fi SSID/password, hostname (default `rnet-bridge-xxxx`), optional API token, OTA password.
- Settings are stored in NVS (`Preferences`).
- Factory reset / re-enter portal: **double-reset detection** (press reset twice within 3 s), because many D1 Mini ESP32
  boards have no user button. Also expose a `factory_reset` HTTP endpoint (authenticated).

### 4.5 Network API (contract — also written to `docs/protocol.md`)
**Discovery:** mDNS service `_rnetbridge._tcp`, port 80. TXT records: `id` (MAC-based, stable),
`fw` (firmware version), `proto` (API protocol version, integer, starts at `1`).

**HTTP:**
| Method | Path | Purpose |
|---|---|---|
| GET | `/api/info` | `{id, name, fw, proto, model:"rnet", zones:6, sources:6, uptime_s, wifi_rssi}` |
| POST | `/api/ota` | Firmware upload (multipart). Requires OTA password if set |
| POST | `/api/factory_reset` | Clears settings, reboots into portal. Requires token if set |
| GET | `/` | Minimal status page (link state, zones table, stats) for troubleshooting |

**WebSocket** at `/ws`. All messages are JSON objects with a `type`. If an API token is configured,
the client must send `{"type":"auth","token":"..."}` first, or it is disconnected.

Server → client:
```jsonc
{"type":"hello","proto":1,"id":"a1b2c3d4e5f6","fw":"1.0.0","zones":6,"sources":6}
{"type":"snapshot","link":{...},"zones":[ {zone_state}, ... 6 entries ]}   // after hello/auth
{"type":"zone_state","zone":3,"valid":true,"power":true,"source":2,"volume":24,
 "muted":false,"bass":0,"treble":-2,"balance":0,"loudness":false,"turn_on_volume":20,
 "shared_source":false,"system_on":true}
{"type":"link","up":true,"stats":{"tx":0,"rx":0,"checksum_errors":0,"timeouts":0,
 "retries":0,"duplicate_replies":0,"queue_depth":0}}
{"type":"result","id":17,"ok":true}                        // or "ok":false,"error":"busy|invalid|link_down|timeout"
{"type":"log","level":"debug","msg":"..."}               // only when subscribed
{"type":"frame","dir":"rx","hex":"F0 00 ..."}            // only when frame tracing enabled
```
Notes:
- `zone` and `source` are **1-based** in the API (the firmware converts to 0-based on the wire).
- `volume` and `turn_on_volume` are raw 0–50. `bass`, `treble`, and `balance` are signed −10…+10 (negative balance = left).
- `valid:false` until the first successful poll of that zone.

Client → server:
```jsonc
{"type":"command","id":17,"cmd":"set_power","zone":3,"value":true}
// cmd ∈ set_power | set_source | set_volume | volume_up | volume_down | set_mute |
//       set_bass | set_treble | set_balance | set_loudness | set_turn_on_volume |
//       all_on | all_off | refresh (zone optional; omitted = all zones)
{"type":"subscribe_logs","level":"info"}      // or "off"
{"type":"trace_frames","enabled":true}
{"type":"raw_send","id":18,"hex":"F0 00 00 7F ..."}   // debug: firmware recomputes checksum if "fix_checksum":true
```
- Heartbeat: the server sends WS ping every 15 s. Clients that don't answer within 30 s are dropped.
- Max 4 simultaneous WS clients.
- **Versioning:** any breaking change bumps `proto`. The integration refuses to connect to an unsupported
  `proto` and raises a repair issue telling the user to update firmware or integration.

### 4.6 OTA
- `POST /api/ota` (used by the integration's future update entity and by `curl`).
- `ArduinoOTA` enabled for `pio run -t upload --upload-port <host>` during development.
- Release builds: GitHub Actions builds `firmware.bin` and attaches it to the release.

---

## 5. Home Assistant integration

### 5.1 Identity
- Domain: `rnet_bridge`. Name: "Russound RNET Bridge". This deliberately avoids clashing with core's
  `russound_rnet` and `russound_rio`.
- `iot_class: local_push`, `config_flow: true`, `integration_type: hub`, `zeroconf: ["_rnetbridge._tcp.local."]`.
- No external `requirements`: the WebSocket client lives in `api.py` and uses `aiohttp` from HA.

### 5.2 Layout
```
custom_components/rnet_bridge/
  __init__.py          # setup/unload entry, create client + coordinator, forward platforms, stale-device cleanup
  manifest.json
  const.py
  api.py               # RnetBridgeClient: connect, auth, hello/proto check, send_command(+await result), listener callbacks
  coordinator.py       # push coordinator: owns client, reconnect w/ exponential backoff (1s→60s), exposes zone states
  config_flow.py       # zeroconf discovery + manual host; options flow for zones/sources
  entity.py            # base entity classes (controller entity, zone entity) and device_info helpers
  media_player.py
  number.py
  switch.py
  button.py
  binary_sensor.py
  sensor.py
  diagnostics.py       # config entry diagnostics: info, link stats, last zone states (redact token)
  strings.json
  translations/en.json
  icons.json
  brand/icon.png, brand/icon@2x.png   # required for HACS (see §5.6)
hacs.json              # repo root
```

### 5.3 Config flow
1. **Discovery:** zeroconf finds `_rnetbridge._tcp` → confirm step showing name and id. The unique id is the
   bridge `id`, so rediscovery with a new IP updates the host. **Manual:** host (+ optional port, token).
2. Validate by connecting, completing hello/auth, and checking `proto`.
3. **Zones step:** for zones 1–6, an "enabled" toggle and a name (defaults "Zone N"). Default: all enabled.
4. **Sources step:** for sources 1–6, an "enabled" toggle and a name (defaults "Source N"). Default: all enabled.
5. **Options flow** re-opens steps 3–4 at any time. Saving reloads the entry. Devices for disabled zones are
   removed from the device registry.

Use `selector`-based schemas. Store zone/source config in `entry.options`. Store host/token in `entry.data`.

### 5.4 Devices & entities
**Controller device** — identifiers `(rnet_bridge, <id>)`, name "Russound Controller", model "RNET Bridge",
`sw_version` = firmware version, configuration_url = `http://<host>/`.
| Entity | Platform | Notes |
|---|---|---|
| All Zones On | button | Only affects zones with "System On Enable" in controller programming |
| All Zones Off | button | |
| Resync | button | Sends `refresh` for all zones |
| RNET Link | binary_sensor | device_class `connectivity`, entity_category `diagnostic` |
| Checksum errors, Timeouts, Duplicate replies | sensor | diagnostic, disabled by default, state_class `total_increasing` |

**Zone device** (one per *enabled* zone) — identifiers `(rnet_bridge, <id>_zone<N>)`, name = configured zone name,
`via_device` = controller device, `suggested_area` = zone name.
| Entity | Platform | Details |
|---|---|---|
| (zone name) | media_player | device_class `speaker`; features TURN_ON, TURN_OFF, VOLUME_SET, VOLUME_STEP, VOLUME_MUTE, SELECT_SOURCE; state ON/OFF; `volume_level` = volume/50; `volume_step` = 0.02; `source_list` = enabled source names in input order |
| Bass | number | −10…10 step 1, mode slider, entity_category `config` |
| Treble | number | same |
| Balance | number | −10…10 (negative = left), `config` |
| Turn-on volume | number | 0–100 step 2, unit `%`, `config` (convert ÷2 to raw) |
| Loudness | switch | `config` |

Rules:
- The media player uses `_attr_has_entity_name = True` with `name=None`, so the entity is named after the zone device.
- If a zone's source is not an enabled source (e.g. selected at the keypad), report `source` = `"Source N"`.
- All entities become **unavailable** when the WS connection is down, the RNET link is down, or `valid:false`.
- Commands await the `result` message (timeout 5 s). On error, raise `HomeAssistantError` with a translated message.
- No optimistic state. Wait for the confirming `zone_state` (it arrives about 150 ms after the command thanks to the priority poll).

### 5.5 Services
- `rnet_bridge.send_raw` (developer/debug): `config_entry_id`, `hex`, `fix_checksum`. Logs the result.

### 5.6 HACS compatibility (required)
The repo must be installable as a HACS **custom repository** from day one, and eligible for the HACS default
store later. Requirements (per hacs.xyz "Publish → Integration"; re-check the current page before release):
- **Exactly one** integration under `custom_components/` at the repo root (`custom_components/rnet_bridge/`).
  Every file the integration needs at runtime lives inside that folder. `firmware/`, `tools/`, and `docs/` at
  the root are ignored by HACS, so the monorepo is fine.
- **`hacs.json`** at the repo root:
  ```json
  {
    "name": "Russound RNET Bridge",
    "homeassistant": "2026.3.0",
    "render_readme": true
  }
  ```
  Set `homeassistant` to the minimum HA version actually tested. 2026.3 is the floor because of inline brand assets (below).
- **`manifest.json`** must include at least `domain`, `name`, `version`, `documentation`, `issue_tracker`,
  `codeowners`. Keys are ordered `domain`, `name`, then alphabetical (hassfest enforces this):
  ```json
  {
    "domain": "rnet_bridge",
    "name": "Russound RNET Bridge",
    "codeowners": ["@<github-user>"],
    "config_flow": true,
    "dependencies": [],
    "documentation": "https://github.com/<github-user>/rnet-bridge",
    "integration_type": "hub",
    "iot_class": "local_push",
    "issue_tracker": "https://github.com/<github-user>/rnet-bridge/issues",
    "requirements": [],
    "version": "0.1.0",
    "zeroconf": ["_rnetbridge._tcp.local."]
  }
  ```
- **Brand assets:** HACS requires them. Ship them inline at `custom_components/rnet_bridge/brand/icon.png`
  (256×256) and `icon@2x.png` (512×512), plus optional `logo.png` / `logo@2x.png`. Use an original icon
  (e.g. a generic speaker/zones glyph), **not** Russound's logo or trademark. If inline brands turn out not to
  satisfy the HACS validator at release time, fall back to a PR to `home-assistant/brands`.
- **Versioning & releases:** SemVer. `manifest.json` `version` must equal the GitHub Release tag (`v0.1.0` ↔ `0.1.0`).
  HACS shows the latest 5 releases. Publish real GitHub Releases, not just tags.
  The same release attaches the matching `firmware.bin` (one repo version = matched firmware + integration).
  Release notes state the API `proto` version.
- **Repo hygiene:** public GitHub repo with a description and topics (`home-assistant`, `hacs`, `integration`,
  `russound`, `esp32`), a README with HACS install steps (custom repository → Integration) and a "My Home Assistant" badge, and a LICENSE (MIT).
- **CI:** `.github/workflows/validate.yml` runs `hacs/action` (category `integration`) and `home-assistant/actions/hassfest`
  on push, PR, and a weekly schedule. Both must pass before any release.
- **Release automation:** `.github/workflows/release.yml`, on a published release: verify tag == manifest version,
  build firmware, attach `firmware.bin` and `rnet_bridge.zip` (zip of the integration folder).
  Optionally set `"zip_release": true` + `"filename": "rnet_bridge.zip"` in `hacs.json` later.
- Because the integration has no PyPI dependency (`requirements: []`), nothing outside this repo needs publishing.

### 5.7 Quality bar
Aim for HA integration quality scale "silver" practices: config flow tests, unique ids, `has_entity_name`,
entity translations, diagnostics, reauth if a token is added later, unavailable handling, no blocking I/O, strict typing.

---

## 6. Dev tools (`tools/`)
- `fake_bridge.py` — asyncio WebSocket server implementing the §4.5 API with a simulated 6-zone controller.
  Lets the HA integration be built and tested with no hardware. It is also used as a pytest fixture.
- `rnet_decode.py` — decode hex dumps (from USB serial logs or frame traces) into annotated RNET frames.
- `rnet_cli.py` (optional) — talk to the bridge's WS from a terminal (send commands, tail logs/frames).

---

## 7. Repository layout
```
/
  CLAUDE.md
  README.md
  LICENSE
  hacs.json
  custom_components/rnet_bridge/...   # the ONLY folder under custom_components/
  firmware/...
  tools/...
  tests/                       # pytest for the integration (pytest-homeassistant-custom-component)
  docs/
    SPEC.md                    # this specification
    protocol.md                # §4.5 network API contract (source of truth for both halves)
    rnet-notes.md              # hardware-verified RNET findings (ACK format, quirks, captures)
    hardware.md                # wiring, cable verification, photos
    reference/russound-rnet-protocol.pdf
    reference/russound-rnet-protocol.md   # editable transcription of the PDF, with errata
  .github/workflows/
    validate.yml               # hacs/action + hassfest (push, PR, weekly)
    ci.yml                     # firmware build + native tests; ruff, mypy, pytest
    release.yml                # tag==manifest check, attach firmware.bin + rnet_bridge.zip
  pyproject.toml               # ruff, mypy, pytest config
```

---

## 8. Testing strategy
- **Firmware unit tests** (`pio test -e native`): all §3.10 vectors; escape round-trip for every byte 0x80–0xFF;
  streaming parser resync (garbage before F0, truncated frames, bad checksum); all-zone-info parsing;
  engine behaviour with a scripted fake controller (ACK sent after replies, timeout → retry → link down,
  priority poll after command, queue-full error, mute emulation edge cases).
- **Integration tests** (pytest): config flow (zeroconf, manual, invalid host, unsupported proto), options flow
  removing a zone removes its device, entity states from fake bridge snapshots, command → result path,
  reconnect after server drop, unavailable states.
- **Hardware-in-the-loop** (manual, documented checklists in `docs/rnet-notes.md`): see milestones below.
- **Soak test:** 48 h running with HA connected. Zero stuck states, zero firmware resets, stats reviewed.

---

## 9. Milestones / bring-up plan
Each milestone ends in something testable. Hardware steps need the human.

- **M0 — Wiring.** Verify DB9 pinout with a multimeter (§2.3) and obtain the right cable. Do a MAX3232 loopback test
  (short DB9 pins 2–3, echo bytes from the ESP32).
- **M1 — Codec.** `lib/rnet` codec + message builders/parsers + native tests passing on all vectors.
- **M2 — First contact.** Firmware in "serial console" mode: commands typed over USB serial send frames,
  and all RX frames are hex-dumped and decoded. Confirm: Zone 1 power on/off works, all-zone-info reply parses,
  **determine the correct data-handshake format (§3.5)**, check whether the session-start event is needed
  (§3.6), and check the turn-on-volume reply format. Also run the M2 checks in §11 items 7–10. Record findings in
  `docs/rnet-notes.md` and update the constants.
- **M3 — Engine.** Queue, ACKs, polling, link state, zone model, mute emulation. Keypad changes show up in the
  USB-serial state dump within one poll cycle.
- **M4 — Network.** WiFiManager portal, NVS settings, mDNS, `/api/info`, WebSocket API per `docs/protocol.md`,
  status page, OTA. Exercise with `tools/rnet_cli.py`.
- **M5 — Integration skeleton.** `fake_bridge.py`, `api.py`, coordinator, config flow (zeroconf + manual), controller device.
  Create `hacs.json`, a complete `manifest.json`, placeholder brand icons, and `validate.yml` **now**, so HACS/hassfest
  validation runs green from the first integration commit. The repo can be added to HACS as a custom repository from here on.
- **M6 — Entities.** Media players, numbers, switch, buttons, sensors; options flow for zones/sources; stale-device cleanup.
- **M7 — Polish & release.** Diagnostics, translations, `send_raw`, repair issue for proto mismatch, final brand icons,
  README with HACS install instructions, `release.yml`, first GitHub Release `v0.1.0` installed via HACS on the real HA instance.
- **M8 — Hardening.** Soak test; Wi-Fi drop and controller power-cycle recovery; HA restart; firmware OTA from HA host.

---

## 10. Acceptance criteria (v1)
- From HA, every enabled zone can be powered, have its source selected, have volume set/stepped, and be muted/unmuted.
  The UI reflects confirmed state within 1 s.
- Bass, treble, balance, loudness, and turn-on volume can be read and set from the zone device page.
- A change made at the keypad appears in HA within 5 s.
- Enabling/disabling zones or sources and renaming them in the options flow requires no firmware change.
- After a controller power cycle, a Wi-Fi outage, or an HA restart, everything recovers without manual action.
- No 2.5 s stalls during normal operation (`duplicate_replies` stays at 0 in the soak test).
- Native tests and integration tests pass in CI. hassfest and HACS validation pass.
- The integration installs, updates, and uninstalls cleanly through HACS as a custom repository from a GitHub Release,
  and that release carries the matching `firmware.bin`.

---

## 11. Open questions to resolve during bring-up
1. Exact data-handshake (ACK) frame (§3.5).
2. Whether the CAA6.6 requires/accepts the Device Restart event to unlock zone Get/Set (§3.6).
3. Turn-on volume Get reply format (§3.4, §3.9).
4. Whether tone/turn-on-volume Set Data works while a zone is off.
5. Whether the controller emits any unsolicited frames when keypads are used (could reduce polling).
6. Whether keypad displays need a nudge to refresh after a Set Data change (doc says they won't auto-update).
   This is acceptable for v1, but should be noted.
7. **Is the ACK needed at all with keypad ID `0x70`?** PDF §11 says a Peripheral Source "does not require
   Handshaking when the keypad ID used to send data is 0x70", and recommends `0x60` when handshaking is wanted.
   M2 test: send `Get all zone info` with **no** ACK, and watch whether the reply is retransmitted about
   2.5 s later. Repeat with the hypothesised ACK (§3.5). Record both results.
8. **Zone On/Off Source Zone ID (byte 6).** Does the controller accept `zz` (PDF §8.1.1 template, SPEC §3.3)
   and `00` (PDF §13 hex table)? Test Zone 2 On both ways.
9. **All Zones On on the CAA6.6.** The PDF Quick Reference says it is unsupported on the CAA66. Test whether it
   does anything. If not, drop or hide the All Zones On button (§5.4).
10. **Get Volume reply value.** Check whether PDF §8.3.2's "Volume Level -1" is real, by comparing it against
    all-zone-info byte 23. This only matters if discrete Get Volume is ever used.
