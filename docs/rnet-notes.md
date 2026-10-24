# RNET hardware notes

Facts verified on real hardware. These override the Russound PDF (CLAUDE.md rule 2). Each entry gives
the date, what was tested, and the raw evidence (console output). The SPEC §11 open questions are
answered here, and the matching constants in `firmware/include/config.h` are updated from them.

## Hardware

| Item | Value | How verified |
|---|---|---|
| Controller | Russound CAA6.6 | — |
| Bridge board | Olimex ESP32-POE-ISO (WROOM) | _fill in: `esptool flash_id` output (chip, flash size) and board revision printed on the PCB_ |
| RS-232 module | Olimex MOD-RS232 Rev A on UEXT | Schematic: T1OUT → DB9 pin 2, R1IN ← DB9 pin 3 |
| UART pins | GPIO4 TX, GPIO36 RX (UART1) | ESP32-POE-ISO Rev O schematic, UEXT pins 3/4 |
| Cable | DB9 male-to-male null-modem | _fill in: loopback/first contact result_ |

## Flashing and the console

From the `firmware/` directory (`pio` is not on PATH on the dev PC, so use `python -m platformio`):

```powershell
python -m platformio run -e esp32 -t upload          # add --upload-port COMx if several ports exist
python -m platformio device monitor -b 115200        # Ctrl+C to exit
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

Result: _pending_

### M0.2 Controller connection
1. Disconnect the Moxa. Disable the `russound_rnet` integration in HA.
2. Connect MOD-RS232 ↔ controller with the null-modem cable.
3. Optional: with the controller on, its DB9 pin 2 should read −5 to −9 V against pin 5.

Result: _pending_

### M2.1 Passive listen (SPEC §11.5)
Type nothing. Change volume/source/power at a keypad and watch for RX lines. Any frames that appear
are unsolicited and worth recording: they could reduce polling later.

Result: _pending_

### M2.2 Session start (SPEC §11.2)
For a clean test, power-cycle the controller first, so it has seen no events since boot.
1. `info 1`. Is there a `ZoneInfo z1 ...` reply?
2. If not: `restart`, then `info 1` again.
3. If still nothing, send any harmless event (for example `volup 1` then `voldn 1`), then `info 1`.

Record which step first produced a reply. That decides `config::kSessionStartEvent`.

Result: _pending_

### M2.3 Zone power and source-zone byte (SPEC §11.8)
1. `pwrsrc 00`, then `on 1` / `off 1`. Does zone 1 follow?
2. `pwrsrc zz`, then `on 2` / `off 2`. Does zone 2 follow?
3. `pwrsrc 00`, then `on 2` / `off 2` (the default form for a zone other than 1).

Result: _pending_

### M2.4 Data handshake / ACK (SPEC §3.5, §11.1, §11.7)
1. `autoack off`. Run `info 1` and wait 5 s. Does a `!! DUPLICATE` appear about 2.5 s later?
   - **No duplicate:** the controller doesn't need an ACK from keypad 0x70 (§11.7). Record that and skip the rest.
2. `autoack on` (acktype 0x02, the hypothesis). Run `info 1` and wait 5 s. Any duplicate?
3. If yes: `acktype 06` and repeat. Then try other values if needed, and record each result.
4. Note any `Handshake` frames the controller sends after `set ...` commands. Those are its ACKs to us.

Result (working ACK type, or "not needed"): _pending_

### M2.5 All-zone-info parsing (SPEC §3.4)
For each zone 1–6: `info <z>`. Check that each decoded line matches the keypad (power, source,
volume ×2, bass, treble, loudness, balance). Paste one full RX line.

Result: _pending_

### M2.6 Turn-on volume reply format (SPEC §11.3)
`get 1 tov`. A decoded `ZoneParam z1 tov=..` means the expected format is right. If the line shows
`(INVALID reply)` or raw `tpath=/spath=` instead, paste the hex. That is the real format.

Result: _pending_

### M2.7 Tone/turn-on-volume while a zone is off (SPEC §11.4)
`off 1`, `set 1 bass 3`, `on 1`, `info 1`. Does `bass=3` show? Restore with `set 1 bass 0`.

Result: _pending_

### M2.8 Keypad display refresh (SPEC §11.6)
After `set 1 bass 3` with zone 1 on, does the keypad's bass display update on its own?

Result: _pending_

### M2.9 All On / All Off on the CAA6.6 (SPEC §11.9)
`alloff`, then `allon`. Do any zones turn on? Only zones set to "System On Enable" are affected.

Result: _pending_

### M2.10 Get Volume reply offset (SPEC §11.10)
Discrete Get Volume isn't a console command; send it raw for zone 1:
`rawfix F0 00 00 7F 00 00 70 01 04 02 00 00 01 00 00`. Compare the value byte with `info 1`'s `vol=`.

Result: _pending_
