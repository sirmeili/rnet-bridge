# RNET Bridge network API (proto 1)

The contract between the bridge firmware and its clients: the Home Assistant integration, `tools/fake_bridge.py`
and `tools/rnet_cli.py`. **This file is the source of truth** (CLAUDE.md golden rule 1). Any change updates the
firmware, the integration, `fake_bridge.py` and this file together, and a breaking change bumps `proto`.

Conventions:
- Zones and sources are **1-based** (1–6). The firmware converts to the 0-based RNET wire values.
- `volume` and `turn_on_volume` are raw **0–50** (keypads display ×2). `bass`, `treble` and `balance` are signed
  **−10…+10** (negative balance = left).
- All JSON objects may gain new fields in later firmware without a `proto` bump. Clients must ignore fields they
  don't know.

## Discovery
mDNS service `_rnetbridge._tcp.local.`, port 80, re-announced when the bridge switches between Ethernet and Wi-Fi.

| TXT key | Value |
|---|---|
| `id` | Stable bridge id: the lowercase hex Ethernet MAC, e.g. `a1b2c3d4e5f6` |
| `fw` | Firmware version, e.g. `0.0.3` |
| `proto` | API protocol version, an integer (`1`) |

## HTTP
| Method | Path | Auth | Purpose |
|---|---|---|---|
| GET | `/api/info` | none | Bridge information (below) |
| POST | `/api/ota` | OTA password, if set | Firmware upload, `multipart/form-data` with one file field. Reboots on success |
| POST | `/api/factory_reset` | API token, if set | Clears all settings and reboots into the setup portal |
| GET | `/` | none | Status page for troubleshooting (HTML; not part of the contract) |

Auth is an `Authorization: Bearer <secret>` header: the OTA password for `/api/ota`, the API token for
`/api/factory_reset`. When the secret isn't configured, no header is needed. A missing or wrong secret returns
`401`.

`GET /api/info` returns:
```json
{"id":"a1b2c3d4e5f6","name":"rnet-bridge-e5f6","fw":"0.0.3","proto":1,"model":"rnet","zones":6,"sources":6,
 "uptime_s":3600,"network":"ethernet","wifi_rssi":null}
```
- `name` is the hostname.
- `network` is `"ethernet"` or `"wifi"`: the interface the bridge is serving on.
- `wifi_rssi` is the Wi-Fi signal in dBm when `network` is `"wifi"`, otherwise `null`.

`POST /api/ota` and `POST /api/factory_reset` return `{"ok":true}` before rebooting, or `{"ok":false,"error":"..."}`
with a 4xx/5xx status.

## WebSocket `/ws`
Every message is a JSON text frame with a `type`.

### Session
1. On connect, the server sends `hello`.
2. If `auth_required` is `true`, the client must send `auth` within 5 s. A wrong token or no `auth` in time gets
   `{"type":"error","error":"unauthorized"}`, then the server closes the connection.
3. Once authorized (or straight after `hello` when no token is set), the server sends `snapshot`, then pushes
   `zone_state` and `link` messages as things change.

The server pings every 15 s and drops a client that hasn't answered (pong or any message) for 30 s. At most 4
clients are served at once; a 5th is closed straight after connecting.

### Server → client
```jsonc
{"type":"hello","proto":1,"id":"a1b2c3d4e5f6","fw":"0.0.3","zones":6,"sources":6,"auth_required":false}

{"type":"snapshot","link":{link},"zones":[{zone_state}, ... 6 entries, zones 1-6 in order]}

{"type":"zone_state","zone":3,"valid":true,"power":true,"source":2,"volume":24,"muted":false,
 "bass":0,"treble":-2,"balance":0,"loudness":false,"turn_on_volume":20,"shared_source":false,"system_on":true}

{"type":"link","up":true,"stats":{"tx":0,"rx":0,"checksum_errors":0,"dropped":0,"timeouts":0,"retries":0,
 "duplicate_replies":0,"handshakes_rx":0,"queue_depth":0}}

{"type":"result","id":17,"ok":true}
{"type":"result","id":17,"ok":false,"error":"busy"}      // error: busy | invalid | link_down

{"type":"log","level":"info","msg":"..."}               // only while subscribed
{"type":"frame","dir":"rx","hex":"F0 00 ..."}           // only while frame tracing is on; dir: rx | tx
{"type":"error","error":"unauthorized"}                 // then the connection closes
```
- `zone_state` is sent when anything in it changes. Inside `snapshot`, entries have the same fields without
  `type`.
- `valid` is `false` until the zone's first successful poll. The other fields are then meaningless.
- `turn_on_volume` is `null` until it has been read.
- `source` is only meaningful while `power` is `true`. For a zone that is off, the CAA6.6 reports its turn-on
  volume as `volume` (SPEC §3.4).
- `muted` is emulated by the bridge (SPEC §4.3) and resets on reboot.
- `link` is sent when `up` changes and every 30 s, carrying the engine counters (all cumulative since boot).

### Client → server
```jsonc
{"type":"auth","token":"..."}

{"type":"command","id":17,"cmd":"set_power","zone":3,"value":true}

{"type":"subscribe_logs","level":"info"}    // level: debug | info | warn | error | off
{"type":"trace_frames","enabled":true}
{"type":"raw_send","id":18,"hex":"F0 00 00 7F 00 00 70 01 04 02 00 00 07 00 00","fix_checksum":true}
```

`id` is chosen by the client (an unsigned 32-bit integer) and echoed in the `result`. Every `command` and
`raw_send` gets exactly one `result`.

| `cmd` | `zone` | `value` |
|---|---|---|
| `set_power` | 1–6 | `true` / `false` |
| `set_source` | 1–6 | 1–6 |
| `set_volume` | 1–6 | 0–50 |
| `volume_up`, `volume_down` | 1–6 | — |
| `set_mute` | 1–6 | `true` / `false` |
| `set_bass`, `set_treble`, `set_balance` | 1–6 | −10…10 |
| `set_loudness` | 1–6 | `true` / `false` |
| `set_turn_on_volume` | 1–6 | 0–50 |
| `all_on`, `all_off` | — | — |
| `refresh` | 1–6, or omitted for all zones | — |

Result errors:
- `busy`: the command queue (32) is full.
- `invalid`: unknown `cmd`, a missing or out-of-range field, or a volume step or mute before the zone's first
  poll. Also bad hex in `raw_send`.
- `link_down`: the RNET link was down when the command came up for sending.

`ok: true` means the frame was **sent**. The controller doesn't acknowledge events, so confirmation is the
`zone_state` from the priority poll about 150 ms later. All On does nothing on the CAA6.6 (SPEC §11 item 9).

`raw_send` (debug) sends the given bytes as one frame. With `fix_checksum: true`, `hex` is F0 through the last body
byte and the bridge appends the checksum and F7. It goes through the command queue, so it can also fail `busy`
or `link_down`.

A message with an unknown `type`, or one that isn't valid JSON, gets `{"type":"error","error":"bad_message"}`
and is otherwise ignored.
