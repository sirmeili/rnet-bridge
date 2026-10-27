# RNET Bridge enclosure

3D-printable case for the bridge hardware (SPEC §2): an Olimex ESP32-POE-ISO (rev J+) and an Olimex
MOD-RS232 (rev A), mounted in a line.

```
front wall                                         back wall
[RJ45]=== ESP32-POE-ISO ===|=== MOD-RS232 ===[DB9]
          micro-USB through the left wall
```

Outside size: 37 × 146.6 × 33 mm (W × L × H, lid included).

## Files

| File | What it is |
|---|---|
| `rnet-bridge.scad` | The model (OpenSCAD). Every dimension is a named variable at the top of the file. |
| `manifest.json` | Parts list for an external 3D model viewer (used to share a viewable link). Not used by the firmware or the integration. |

## Rendering

Choose the part with `part`, either in the OpenSCAD Customizer or on the command line:

```powershell
openscad -D 'part="base"' -o base.stl rnet-bridge.scad
```

| `part` | Print? | Notes |
|---|---|---|
| `base` | yes | Flat bottom, printed as modelled. |
| `lid` | yes | Already flipped into print orientation. |
| `plunger` | yes, ×2 | Reset/user button plungers. Print standing on the cap end, with a brim. |
| `lid_inlay` | optional | The lid text and LED labels as separate bodies, for a second-colour inlay. |
| `test_front`, `test_back`, `test_lid_leds` | optional | Small slices for checking the RJ45, DB9, and light-pipe fit before a full print. |
| `assembly`, `fit`, `inlay_check` | no | Previews with stand-in boards. |
| `lid_asm`, `plunger_asm`, `g_*` | no | Parts in assembled position, for the viewer (`manifest.json`). |

Exported STL/3MF files aren't committed. Generate them from the `.scad` file.

## Hardware

- 6 × M2.5 self-tapping screws hold the lid on (3 per side).
- The boards need no screws. The ESP32 sits on 3 locating pins and the MOD-RS232 on 4, and posts on the
  lid clamp both boards down.
- Optional: 3 × Bivar PLP5-2-625 light pipes over the PWR/LNK/ACT LEDs (`light_pipes`).
- Optional: two 12.7 mm hook-and-loop straps or zip ties through the floor slots for rail mounting
  (`strap_slots`). The flat bottom also suits adhesive strips. Screw tabs are available as `mount_ears`.

## Measure before printing

These values in `rnet-bridge.scad` are marked `<-- measure` / `<-- check`. Confirm them on the real
boards, or print the test slices:

- `db9_above_pcb`: height of the DB9 centre above the bottom of the MOD-RS232 PCB.
- `db9_wide_up`: which way up the DB9's D shape faces.
- `btn_switch_h`, `plunger_tip_d`: button height and actuator size, for the plungers.
- `pipe_leds`: LED positions, taken from the Olimex rev J drawing.
