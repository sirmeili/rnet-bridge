# RNET hardware notes

Facts verified on real hardware. These override the Russound PDF (CLAUDE.md rule 2). Each entry gives
the date, what was tested, and the raw evidence (console output). The SPEC §11 open questions are
answered here, and the matching constants in `firmware/include/config.h` are updated from them.

## Hardware

| Item | Value | How verified |
|---|---|---|
| Controller | Russound CAA6.6 | — |
| Bridge board | Olimex ESP32-POE-ISO (WROOM) | 2026-11-02 `esptool flash_id`: ESP32-D0WD-V3 (rev v3.1), 40 MHz crystal, 4 MB flash, USB-serial CH340 (VID:PID 1A86:7523). _Board revision printed on the PCB: fill in_ |
| RS-232 module | Olimex MOD-RS232 Rev A on UEXT | Schematic: T1OUT → DB9 pin 2, R1IN ← DB9 pin 3 |
| UART pins | GPIO4 TX, GPIO36 RX (UART1) | ESP32-POE-ISO Rev O schematic, UEXT pins 3/4 |
| Cable | DB9 male-to-male null-modem | 2026-11-02: loopback PASS through the cable (M0.1), and replies received in both directions (M2.2) |

## Flashing and the console

The bring-up console used for this checklist is the `esp32-console` build env (`src/console/`). The
default `esp32` env is the engine firmware (M3 onward). From the `firmware/` directory (use
`python -m platformio` where `pio` isn't on PATH):

```powershell
pio run -e esp32-console -t upload          # add --upload-port COMx if several ports exist
pio device monitor -b 115200                # Ctrl+C to exit
```

Power the board from USB only during bring-up. Ethernet/PoE isn't needed yet; the ISO board is safe
with both connected. Type `help` in the monitor for the command list. Every received frame is
printed as hex plus a decoded line, and `!! DUPLICATE` marks a retransmitted reply.

## Bring-up checklist

Work through these in order and paste the relevant console output under each step.

### M0.1 MOD-RS232 loopback (no controller needed)
1. With the MOD-RS232 on the UEXT connector, short **DB9 pins 2 and 3** on its female connector (a
   jumper wire or bent paperclip). Nothing else connected.
2. Run `loopback`. Expected: `loopback: PASS (64/64 bytes echoed intact)`.
3. Remove the short.

Result: **PASS (2026-11-02).** Firmware 0.0.2. The short was across pins 2–3 at the far (male) end of the
null-modem cable attached to the MOD-RS232, so the cable's pin 2/3 conductors are verified too. This can't tell
a null-modem cable from a straight one; M0.2 does that.
```
> loopback
loopback: sent 64 bytes, waiting for echo...
> loopback: PASS (64/64 bytes echoed intact)
> stats
tx=0 rx=0 checksum_errors=0 dropped=0 timeouts=0 duplicates=0 handshakes_rx=0 acks_tx=0
autoack=off acktype=0x02 pwrsrc=00
```

### M0.2 Controller connection
1. Disconnect the Moxa. Disable the `russound_rnet` integration in HA.
2. Connect MOD-RS232 ↔ controller with the null-modem cable.
3. Optional: with the controller on, its DB9 pin 2 should read −5 to −9 V against pin 5.

Result: _pending_

### M2.1 Passive listen (SPEC §11.5)
Type nothing. Change volume/source/power at a keypad and watch for RX lines. Any frames that appear
are unsolicited and worth recording: they could reduce polling later.

Result (2026-11-02, firmware 0.0.2, bridge sent nothing, `tx=0`): **the controller's RS-232 port repeats keypad
bus traffic** (SPEC §11.5: yes). The controller → bridge direction of the cable works: 266 frames, 0 checksum
errors, 0 dropped. Bridge → controller is still untested.

Controlled test on the only keypad (utility room, zone 1). It starts at zone 1 on, source 2, volume 68:

| Pressed | Keypad event (source `00.00.00`, target `00.00.7F`) | Controller → zone 1 keypads (`00.00.7D`, type `06`) |
|---|---|---|
| Vol Up, tapped twice | `7F`, then `97` 0.2 s later (`97` = second tap/hold?) | body `23 0A 90 01 10`, then `24 ...`: volume 35, 36 (shows 70, 72) |
| Vol Up | `7F` | `25 ...`: 37 (74) |
| Vol Down ×2 | `80` (sent as `F1 7F`) each | `24 ...`, `23 ...`: 72, 70 |
| Source ×4 | `6B` each | `43 02`, `44 03`, `41 00`, `42 01`: sources 3, 4, 1, 2 |

- Keypad events are `F0 00 00 7F 00 zz 00 05 03 02 00 00 02 04 03 <id> 00 00 00 00 00 01 cs F7`, with button ids
  from protocol §6.1 (`6B` Source, `7F`/`80` Vol Up/Down). Other ids seen in an earlier session: `64` Setup,
  `68` Next, `69`/`6A` Plus/Minus, `98` (after Vol Down; hold?). **Power (`6C`) was never seen**, because the
  keypad's power button is broken.
- After each source change the controller broadcasts `7E.zz.00` event `C8` with data = source bitmask (`01`,
  `02`, `04`, `08`), and `00.7D.ss` event `27` with data = source index. It repeats `7E.7D.zz` event `C8` every
  60 s.
- Sources only cycle 1–4, so sources 5–6 are disabled in the controller's programming.
- The console's `!! DUPLICATE` flag fires on repeated keypad presses. It was written for retransmitted replies
  to the bridge, so ignore it for keypad traffic.
- An earlier session's traffic was all wire zone `03` (zone 4) from keypad `00` and `7C`, with Setup/Next/Plus/Minus
  presses. The keypad was set to zone 4 then; the human switched it to zone 1 afterwards (keypad menu), which
  matches the zone 1 traffic above and confirms wire zone = zone − 1. The keypad ended on volume 70 as decoded. In
  that session, keypads ACKed the controller's
  SetData with `F0 00 00 7F 00 03 7C 02 02 7B F7` (handshake type `02`, the §3.5 hypothesis shape) and sent type
  `06`/`07` handshakes after events and type-`06` frames.

### M2.2 Session start (SPEC §11.2)
For a clean test, power-cycle the controller first, so it has seen no events since boot.
1. `info 1`. Is there a `ZoneInfo z1 ...` reply?
2. If not: `restart`, then `info 1` again.
3. If still nothing, send any harmless event (for example `volup 1` then `voldn 1`), then `info 1`.

Record which step first produced a reply. That decides `config::kSessionStartEvent`.

Result (2026-11-02): **step 1 replied, with no session-start event.** The bridge had transmitted nothing before
this (`tx=0`). The first run came after keypad events. **Clean repeat, 2026-11-02:** the controller was
power-cycled with no keypad use, and the bridge sent nothing during boot. Then `info 1` as its first frame got
a reply in 40 ms (`ZoneInfo z1 off src=1 vol=35 ... sys=off`). So `config::kSessionStartEvent = kNone`.
On boot the controller sends just three frames: event `D4` to `7D.00.00`, event `C5` to `7E.00.00`, and a
SetData broadcast to `7F.00.00` (path `01.01`) containing ASCII `V2.00.00`, probably its firmware version:
```
RX F0 7F 00 00 00 00 7F 00 02 01 01 02 01 01 00 00 01 00 10 00 00 F1 69 00 56 32 2E 30 30 2E 30 30 00 5A 5A 5A 5A 12 F7
```
After the power-cycle, zone 1 came back off with source 1, volume 35. This also confirms
M0.2: the null-modem cable works in both directions. The reply arrived 40 ms after the request and matches the
keypad (zone 1 on, source 2, volume 70 = raw 35) and the SPEC §3.4 layout byte for byte:
```
TX F0 00 00 7F 00 00 70 01 04 02 00 00 07 00 00 7C F7 (get all zone info)
RX F0 00 00 70 00 00 7F 00 00 04 02 00 00 07 00 00 01 00 0C 00 01 01 23 0A 0A 00 0A 01 00 00 00 00 5D F7
   SetData 00.00.70<-00.00.7F ZoneInfo z1 on src=2 vol=35 bass=0 treble=0 loud=off bal=0 sys=on shared=no
```

### M2.3 Zone power and source-zone byte (SPEC §11.8)
1. `pwrsrc 00`, then `on 1` / `off 1`. Does zone 1 follow?
2. `pwrsrc zz`, then `on 2` / `off 2`. Does zone 2 follow?
3. `pwrsrc 00`, then `on 2` / `off 2` (the default form for a zone other than 1).

Result (2026-11-02): **all three work.** Each was confirmed with `info <z>` afterwards. The controller accepts
both `00` and `zz` in byte 6 for Zone On/Off (§11.8). Keep the default `00` (PDF §13 table).
```
TX F0 00 00 7F 00 01 70 05 02 02 00 00 F1 23 00 01 00 01 00 01 14 F7 (on, pwrsrc zz)
TX F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 01 00 01 00 01 13 F7 (on, pwrsrc 00)
   then: ZoneInfo z2 on src=4 vol=23 ...
```
- The controller sends **no handshake** to the bridge after these events, and nothing is retransmitted.
- Zone 1 `off` → `on` restores source 2 and volume 35. With every zone off, all-zone-info reports `sys=off`.
- **The source of a zone that is off isn't meaningful.** Zone 2 read `src=1` while off, came on at source 4, and
  then read `src=4` when off again. The integration shouldn't rely on it while a zone is off.
- Each power change triggers controller → keypad updates (`00.zz.7D`) and a broadcast `7E.zz.70` event `C8`
  whose data is the bitmask of sources in use system-wide (e.g. `0x0A` = sources 2 and 4).

### M2.4 Data handshake / ACK (SPEC §3.5, §11.1, §11.7)
1. `autoack off`. Run `info 1` and wait 5 s. Does a `!! DUPLICATE` appear about 2.5 s later?
   - **No duplicate:** the controller doesn't need an ACK from keypad 0x70 (§11.7). Record that and skip the rest.
2. `autoack on` (acktype 0x02, the hypothesis). Run `info 1` and wait 5 s. Any duplicate?
3. If yes: `acktype 06` and repeat. Then try other values if needed, and record each result.
4. Note any `Handshake` frames the controller sends after `set ...` commands. Those are its ACKs to us.

Result (working ACK type, or "not needed"): step 1, 2026-11-02, `autoack off`: after `info 1`, there was **no
retransmission in the following 6 s** (duplicates counter unchanged, `acks_tx=0`). So for a Request from keypad
`0x70` the controller doesn't expect an ACK (§11.7, consistent with PDF §11). In 40 requests and commands across
M2.2–M2.10, nothing the controller sent the bridge was ever retransmitted, all with `autoack off`. **Conclusion:
no data ACK is needed for keypad `0x70`.** Steps 2–3 weren't needed.

Step 4: after each Set Data (`set 1 bass ...`), the controller sends the bridge **`F0 00 00 70 00 00 7F 02 02 6C
F7`**, a handshake type `02` targeted at `00.00.70`, within about 25 ms. After events (power, All Off) it sends
no handshake.

### M2.5 All-zone-info parsing (SPEC §3.4)
For each zone 1–6: `info <z>`. Check that each decoded line matches the keypad (power, source,
volume ×2, bass, treble, loudness, balance). Paste one full RX line.

Result (2026-11-02): **all six zones reply** in about 40 ms with the §3.4 layout, including zones that are off.
Zone 1 matches the keypad. Zones 2–6 have no keypad to compare against, but their distinct tone values suggest
real stored settings (spot-check: zone 4 should show bass/treble +4).

| Zone | Power | Source | Volume | Bass | Treble | Loud | Balance |
|---|---|---|---|---|---|---|---|
| 1 | on | 2 | 35 | 0 | 0 | off | 0 |
| 2 | off | 1 | 23 | 0 | −1 | off | 0 |
| 3 | off | 1 | 24 | 0 | 0 | on | 0 |
| 4 | off | 1 | 36 | +4 | +4 | off | 0 |
| 5 | off | 1 | 33 | +1 | 0 | off | 0 |
| 6 | off | 1 | 15 | −1 | 0 | off | 0 |
```
TX F0 00 00 7F 00 03 70 01 04 02 00 03 07 00 00 02 F7 (get all zone info)
RX F0 00 03 70 00 00 7F 00 00 04 02 00 03 07 00 00 01 00 0C 00 00 00 24 0E 0E 00 0A 01 00 00 00 00 6A F7
   SetData 00.03.70<-00.00.7F ZoneInfo z4 off src=1 vol=36 bass=4 treble=4 loud=off bal=0 sys=on shared=no
```
No retransmissions with `autoack off` for any of the reads.

### M2.6 Turn-on volume reply format (SPEC §11.3)
`get 1 tov`. A decoded `ZoneParam z1 tov=..` means the expected format is right. If the line shows
`(INVALID reply)` or raw `tpath=/spath=` instead, paste the hex. That is the real format.

Result (2026-11-02): **the expected format is correct** (SPEC §3.4/§3.9). The path is `05 02 00 zz 00 04`, not
the PDF §8.8.3 template's `04 02 00 zz 01 00`. Zone 1 turn-on volume = 35 (70%).
```
TX F0 00 00 7F 00 00 70 01 05 02 00 00 00 04 00 00 7B F7 (get zone param)
RX F0 00 00 70 00 00 7F 00 00 05 02 00 00 00 04 00 00 01 00 01 00 23 25 F7
   SetData 00.00.70<-00.00.7F ZoneParam z1 tov=35
```

### M2.7 Tone/turn-on-volume while a zone is off (SPEC §11.4)
`off 1`, `set 1 bass 3`, `on 1`, `info 1`. Does `bass=3` show? Restore with `set 1 bass 0`.

Result (2026-11-02): **yes, Set Data works while the zone is off** (§11.4). `get 1 bass` read 3 while the zone was
off, and after `on 1`, all-zone-info showed `bass=3` with source and volume unchanged. Restored to 0 and
confirmed.
```
TX F0 00 00 7F 00 00 70 00 05 02 00 00 00 00 00 00 00 01 00 01 00 0D 0B F7 (set zone param)
RX F0 00 00 70 00 00 7F 02 02 6C F7
   Handshake 00.00.70<-00.00.7F type=0x02
```

### M2.8 Keypad display refresh (SPEC §11.6)
After `set 1 bass 3` with zone 1 on, does the keypad's bass display update on its own?

Result (2026-11-02): **the keypad showed +3** when the human opened its bass setting. The controller sent the
keypads no update after the Set Data, so the keypad probably reads the value when the menu opens. No nudge is
needed for tone settings. Not checked: whether an always-visible value (volume on the main display) would
refresh after a Set Data. Volume is set by an event, which the controller does push to keypads (M2.3).

### M2.9 All On / All Off on the CAA6.6 (SPEC §11.9)
`alloff`, then `allon`. Do any zones turn on? Only zones set to "System On Enable" are affected.

Result (2026-11-02): **All Off works; All On does nothing.** After `alloff`, all six zones read off (`sys=off`).
After `allon`, all six still read off. This matches the PDF Quick Reference (All On unsupported on CAA66). It is
possible that no zone has "System On Enable" set in the controller's programming, but either way All On is no
use here: **drop or hide the All Zones On button** (SPEC §5.4). Zone 1 was turned back on afterwards.

### M2.10 Get Volume reply offset (SPEC §11.10)
Discrete Get Volume isn't a console command; send it raw for zone 1:
`rawfix F0 00 00 7F 00 00 70 01 04 02 00 00 01 00 00`. Compare the value byte with `info 1`'s `vol=`.

Result (2026-11-02): **no offset.** Get Volume returned `0x23` = 35, the same as all-zone-info `vol=35`. The PDF
§8.3.2 "Volume Level −1" is a copy-paste error. The console's generic SetData decoder mislabels this reply's
path (`tpath=- spath=02.00.00.01`); the bytes themselves follow the usual layout.
```
TX F0 00 00 7F 00 00 70 01 04 02 00 00 01 00 00 76 F7 (rawfix)
RX F0 00 00 70 00 00 7F 00 00 04 02 00 00 01 00 00 01 00 01 00 23 20 F7
```

## Summary of M2 (2026-11-02)
| SPEC §11 | Answer |
|---|---|
| 1, 7 Data ACK | Not needed for keypad `0x70` (no retransmissions with `autoack off`) |
| 2 Session start | Not needed in this test (caveat: no fresh power-cycle immediately before) |
| 3 Turn-on volume reply | Path `05 02 00 zz 00 04`, as SPEC expected |
| 4 Set Data while off | Works |
| 5 Unsolicited frames | Yes: the port repeats keypad bus traffic (M2.1) |
| 6 Keypad refresh | Tone value showed correctly on the keypad; no nudge needed |
| 8 Zone power byte 6 | `00` and `zz` both work |
| 9 All On | Does nothing on this CAA6.6. All Off works |
| 10 Get Volume offset | None |

Applied 2026-11-02 with the human's OK: `config::kSessionStartEvent = kNone`, `config::kSendDataAck = false`, the
All Zones On entity disabled by default, and SPEC §3.3–§3.9 and §11 updated to match.

## M3 engine check (2026-11-02)
Firmware 0.0.2, `esp32` env (engine + USB state dump), on the same controller.

**Boot:** link up 56 ms after reset. All six zones polled and turn-on volumes read within 2.6 s. 17 requests and 17
replies, with no timeouts or retries.

**Keypad changes reach the model within one poll cycle (≤ 3 s; the M3 done condition).** Zone 1 keypad, one
press about every 10 s:

| Pressed | State dump |
|---|---|
| Vol Up, Vol Up | `vol=36`, `vol=37` |
| Vol Down ×2 | `vol=36`, `vol=35` |
| Source ×4 | `src=3`, `src=4`, `src=2` |

Source 1 lasted less than one poll cycle, so it was never sampled. Polling can miss states shorter than about
3 s, which is acceptable.

**Commands through the engine queue**, each confirmed by the priority poll about 190 ms after sending:
`on 1`; `vol 1 30` → `35`; `mute 1 on` → `vol=0 MUTED` → `mute 1 off` → `vol=35`; `up 1` → 36 → `down 1` → 35;
`set 1 bass 2` → `0`. The controller ACKed each Set Data (`handshakes_rx` +1 each). Everything was restored.

**For a zone that is off, all-zone-info reports its turn-on volume as the volume.** All five off zones read
`vol` equal to their turn-on volume. `set 2 tov 20` on the off zone 2 changed both the turn-on volume reply and the
all-zone-info volume to 20; `set 2 tov 23` restored both.

Counters over about 3 minutes: 392 tx, 427 rx, 0 checksum errors, 0 timeouts, 0 retries, 0 duplicate replies. One
partial frame was dropped, during the board reset at the start.

## M4 network check (2026-11-03)
Firmware 0.0.3 (`esp32` env), board on a PoE switch port plus USB, CAA6.6 connected. Driven from a Windows PC with
`tools/rnet_cli.py` and `curl`, while a USB recorder captured the console.

| Step | Result |
|---|---|
| Boot on Ethernet | Link up 1.7 s after boot, IP at about 4.6 s (DHCP), RNET link up at 2 s |
| mDNS | `_rnetbridge._tcp` found with TXT `id`, `fw`, `proto`. See the discovery note below |
| `/api/info`, status page, 404 | Correct; `network:"ethernet"`, `wifi_rssi:null` |
| WebSocket commands | All `cmd`s confirmed by `zone_state`; `invalid` for volume 51, zone 9 and unknown `cmd`; `raw_send` with `fix_checksum` ok |
| Keypad → WebSocket | Volume and source changes pushed within one poll cycle |
| `trace_frames`, `subscribe_logs` | Frames both directions; log lines delivered live |
| OTA `POST /api/ota` | 1.05 MB in 3 s, `{"ok":true}`, rebooted into the new image |
| Ethernet unplugged, no Wi-Fi saved | Portal opened immediately; phone joined; settings saved; restarted |
| Saved Wi-Fi, no Ethernet | Waited the 10 s boot window, joined Wi-Fi in 2 s, `network:"wifi"`, `wifi_rssi` about -41 |
| Ethernet plugged back in | Switched to Ethernet 3 s after link up, Wi-Fi stopped, mDNS re-announced |
| Double reset (two USB resets within 3 s) | Portal forced alongside Ethernet; closed after 5 min; API resumed |
| `POST /api/factory_reset` | Settings and Wi-Fi credentials erased; came back on Ethernet with no portal |

**Bugs found and fixed during the test:**
- **Portal invisible on channel 1.** WiFiManager's default AP channel is 1. A phone and the PC (fresh WinRT scan)
  never saw it, while a plain `softAP` on channel 6 showed at -17 dBm and the board's own scan saw nearby networks at
  -38 dBm. The portal is now pinned to channel 6. The cause is unconfirmed; RF interference from the 50 MHz RMII clock
  on GPIO17 is a guess.
- **Portal page refused.** The API server still held port 80 when WiFiManager's server started. The API now pauses
  before the portal starts.
- **Saved Wi-Fi not detected at boot.** `WiFi.SSID()` reports the connected network, not the stored one. It now
  reads the station config with `esp_wifi_get_config`.
- **Factory reset left the Wi-Fi credentials.** Erasing needs the Wi-Fi driver started. The reset now runs in the
  main loop and calls `esp_wifi_restore()`.
- Hostname set before the network starts; NVS reads quiet on a fresh board; the log is lock-protected (the OTA
  handler logs from the async_tcp task); a reply soon after a raw send isn't counted as a duplicate; Wi-Fi modem
  sleep off (one `/api/info` call over Wi-Fi had stalled).

**Known issues (not fixed):**
- **Ethernet drops while the Wi-Fi AP runs.** During the 5-minute forced portal, Ethernet went down 3 times and
  recovered each time within about 5 s. There were no drops before the portal or after it closed. Only the forced
  portal runs both radios at once; normal operation uses one interface at a time.
- **WiFiManager blocks about 0.5 s when it starts the AP** (`delay(500)` in `startAP`). That cost one RNET request
  timeout plus a retry, and the late reply was counted in `duplicate_replies`. It happens once per portal start.
- **A VLAN change on the switch port doesn't renew the address.** The link stays up, so the bridge keeps its old
  DHCP address until the lease expires or it reboots. This is ordinary DHCP behaviour. It was seen when the port
  moved from 10.1.0.x to 10.0.0.x.
- **mDNS browsing from a PC with extra adapters** (e.g. WSL's vEthernet) can miss the service; use
  `rnet_cli.py discover --interface <local IP>`. Across subnets it needs an mDNS reflector.

**Not tested on hardware:** API token auth (WebSocket `auth`, `Bearer` on factory reset) and an OTA password. Both are
set through the portal, and none was set. The JSON side is native-tested.

## Turn-on volume moves a playing zone's volume (2026-11-04)
On the CAA6.6, **changing a zone's turn-on volume while the zone is on also sets its current volume to the new
value.** Traced with `rnet_cli.py frames`: the bridge sent only the Set Data, with no Set Volume, and the
controller's next all-zone-info already showed the new volume. Both forms do it:

| Form (PDF) | Frame for zone 1 | Result |
|---|---|---|
| §8.8.2 Set Data (`00`), value 30 | `F0 00 00 7F 00 00 70 00 05 02 00 00 00 04 00 00 00 01 00 01 00 1E 20 F7` | turn-on 30, **volume 30** |
| §8.8.1 Event (`05`), Plus `69` | `F0 00 00 7F 00 00 70 05 05 02 00 00 00 04 00 69 00 00 00 05 00 01 74 F7` | turn-on +1, **volume +1** |

The Event form also makes the controller send the bridge a keypad display message (type `06`); the Set Data
doesn't. For a zone that is off, only the reported volume follows (see the M3 check).

**Workaround (firmware, `EngineConfig::restore_volume_after_turn_on_volume`, on by default):** after a turn-on
volume change on a zone that is on, the engine sends a Set Volume back to the zone's current volume as the next
frame, about 50 ms later. On hardware, zone 1 stayed at volume 34 while its turn-on volume went 18 → 30 → 27 → 18;
the reported volume never showed the change. Whether the 50 ms change is audible is up to the human to judge.
