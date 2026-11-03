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
| `plunger` | yes, ×2 (×1 with `lit_button`) | Reset/user button plungers. Print standing on the cap end, with a brim. |
| `lit_boot` | yes, with `lit_button` | Boot for the lit button. A PLP5 light pipe, cut to the length OpenSCAD echoes, is glued into it. Print wide end down. |
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
- Optional: lit user button (`lit_button`). One more PLP5 light pipe acts as the button plunger. An RGB status
  LED sits in a pocket on the inside of the base's right wall and shines into the pipe through a window in the
  lid's guide tube, so the lid still comes off without the LED. Its wires run down a groove in the wall to the
  floor. The planned LED is a Bambu Lab WS2812 RGB LED (KB003), with the wires soldered to the board and an
  IDC 0.8 mm 4-pin plug on the other end.
- Optional: two 12.7 mm hook-and-loop straps or zip ties through the floor slots for rail mounting
  (`strap_slots`). The flat bottom also suits adhesive strips. Screw tabs are available as `mount_ears`.

## Measure before printing

These values in `rnet-bridge.scad` are marked `<-- measure` / `<-- check`. Confirm them on the real
boards, or print the test slices:

- `db9_above_pcb`: height of the DB9 centre above the bottom of the MOD-RS232 PCB.
- `db9_wide_up`: which way up the DB9's D shape faces.
- `btn_switch_h`, `plunger_tip_d`: button height and actuator size, for the plungers.
- `pipe_leds`: LED positions, taken from the Olimex rev J drawing.

### Lit button LED (KB003)

The pocket and groove were modelled from Bambu's STL and product photo, before the part was in hand.
Once you have it:

- [ ] **Wire bundle diameter** (twisted, and flat where the wires leave the board) → `lit_groove_w`,
  `lit_groove_d`. Keep `lit_groove_d` ≤ 1.2 so at least 0.8 mm of the 2 mm wall remains.
- [ ] **Board thickness, including solder joints on the back** → `lit_led_board[2]`, `lit_pocket_depth`.
  Re-render and check the echoed "LED board to guide tube clearance" is still above 0.
- [ ] **Board width and height** → `lit_led_board[0]`, `lit_led_board[1]`.
- [ ] **Wire exit position** along the board's bottom edge → `lit_groove_y`, so the groove lines up with
  the wires and still falls between two side vents.
- [ ] **Light through the window**: with the boot and pipe fitted, check the colour is visible on top of the
  button, and adjust `lit_led_case_z` / `lit_window_d` if it's dim.

Before wiring it to the ESP32-POE-ISO (not part of the model, but needed for the firmware):

- [ ] **Wire functions**: which colour is power, GND, data in and data out. Use Bambu's KB003
  documentation and/or a meter. Don't go by colour alone.
- [ ] **Supply voltage** the KB003 needs (3.3 V or 5 V). With a 5 V supply, check the LED accepts the ESP32's
  3.3 V data signal.
- [ ] **Connector**: the 0.8 mm plug doesn't fit the board's 2.54 mm EXT header. Cut it off and crimp
  Dupont pins, or use an adapter.
