# Plan: "wait until the zone is off" for turn-on volume (option B)

**Status:** planned, not implemented. **Blocked on decision 2 (where it runs).** Written 2026-11-04 at the human's request. Ships as an option alongside the
current behaviour.

## Problem
On the CAA6.6, changing a zone's turn-on volume while the zone is **on** also sets its current volume to the new
value (`docs/rnet-notes.md`, "Turn-on volume moves a playing zone's volume"). Since v0.1.1 the bridge puts the
volume straight back in the next frame (option A). That's inaudible for small changes, but the human hears it
on big jumps, and **more often with use**. Timing varies: a poll or other traffic can sit between the two frames,
and the controller's own processing time varies.

Option B removes the effect entirely. While a zone is playing, **don't send the turn-on volume change at all.**
Hold it, and send it as soon as the zone is off. On an off zone, a turn-on volume change only moves the
*reported* volume, which nobody hears (M3 check). A turn-on volume only matters the next time the zone comes on,
so applying it when the zone goes off loses nothing.

## User-facing behaviour
A new setting in the integration's options: **"Turn-on volume changes while a zone is playing"**, with three
values:

| Value | What happens | For |
|---|---|---|
| **Wait until the zone is off** | Held, then applied when the zone turns off. The Turn-on volume entity shows the new value straight away, marked as pending | CAA6.6 (no audible effect) |
| **Apply now, then restore the volume** | Today's v0.1.1 behaviour | CAA6.6, if you need the controller updated at once |
| **Apply now** | Sent straight away, with no restore | Controllers that don't move the playing volume (see the README's invitation to open an issue) |

- A zone that's already off: always applied immediately, whatever the setting.
- The pending value survives a Home Assistant restart.
- The Turn-on volume entity gets a `pending` attribute (`true`/`false`), so a dashboard can show it.

## Where it runs
**Recommended: in the integration.** This keeps the firmware config-free (CLAUDE.md golden rule 4) and needs no
API change (`docs/protocol.md` stays at proto 1). The firmware gets one small change: its restore becomes
switchable **per command**, so "Apply now" and "Wait" don't trigger it.
- That's an optional field on `set_turn_on_volume`, `"restore_volume": true | false`, defaulting to `true` (today's
  behaviour). Old clients keep working, and clients ignore unknown fields, so no `proto` bump. `protocol.md`, the
  firmware, the integration and `fake_bridge.py` change together (golden rule 1).

*Alternative: in the firmware.* The bridge itself would hold pending values (in NVS) and apply them when it sees
the zone go off. That works even while Home Assistant is down, but it puts a behaviour setting in the firmware
(against rule 4) and needs a new API field to report pending values. Not recommended unless "applied while HA is
down" matters.

## Integration design
- **Setting:** `turn_on_volume_mode` in `entry.options` (`defer` | `restore` | `immediate`), added to the options
  flow as a third step after sources.
- **Pending store:** `coordinator.pending_turn_on_volume: dict[int, int]` (zone → raw 0–50), persisted with
  `homeassistant.helpers.storage.Store` per config entry (`rnet_bridge.<entry_id>.pending`).
- **Setting the number** (`number.py`):
  - zone off, or mode `immediate`/`restore` → send now (`restore_volume` true only for `restore`);
  - zone on and mode `defer` → store as pending, save, and update the entity. **Not optimistic state** in the
    sense of golden rule 6: it's a value the integration holds and will apply. The entity says so through the
    `pending` attribute, and the controller's real value is never shown as changed.
- **Applying:** a coordinator listener. On every update, for each pending zone whose `zone_state.power` is false,
  send `set_turn_on_volume` (with `restore_volume: false`). Clear the pending value once a pushed `turn_on_volume`
  equals it. This also covers HA starting up, or reconnecting, while the zone is off.
- **Conflicts:**
  - If the controller's turn-on volume changes to something else while a value is pending (someone changed it
    at a keypad), the keypad wins: drop the pending value and log it.
  - If applying fails (`link_down`, `busy`), keep it pending and retry on the next update.
- **Diagnostics:** include pending values.

## Tests
- `fake_bridge.py`: model the CAA6.6 coupling (a turn-on volume Set Data on an on zone sets its volume), behind a
  flag, plus the `restore_volume` field.
- Integration: for each mode, playing and off zones. Defer → no volume change, entity pending, applied on
  power-off, pending cleared on confirmation. Keypad conflict drops the pending value. Pending survives a reload.
  Apply on startup while off. Options flow step.
- Firmware native tests: `restore_volume: false` sends no restore; the default still restores.
- Hardware: with mode `defer`, change a playing zone's turn-on volume. No audible change, the Turn-on volume entity
  shows pending; turn the zone off (from HA and from a keypad) → applied within one poll cycle (~3 s), and the
  keypad shows the new value.

## Decisions
1. **Default mode: `defer` ("wait until the zone is off")**, for new and existing installs. Decided by the human,
   2026-11-04.
2. **Where it runs: OPEN.** Integration (recommended above) or firmware (works without HA running). The human
   wants to think it over. **Settle this before starting work on this feature.**

## Estimate
One release (v0.2.0, since a new option and an API field are added): firmware (small), protocol and fake bridge,
integration (number, coordinator, store, options flow, diagnostics), tests, then one hardware session.
