// =====================================================================
//  INLINE case: Olimex ESP32-POE-ISO (rev J+) + Olimex MOD-RS232 (rev A)
//
//   front wall             back wall
//   [RJ45]=== ESP32-POE-ISO ===|=== MOD-RS232 ===[DB9]
//
//  - Single height, narrow: RJ45 out the front, DB9 out the back,
//    micro-USB through the left wall.
//  - ESP board: 3 locating pins, clamped by posts on the lid (no screws).
//  - MOD-RS232: 4 holes (dia 3.3 mm, 3.175 mm in from each edge).
//    The two holes away from the DB9 take M3 screws into the standoffs;
//    the two under the DB9 body sit on locating pins.
//  - Lid: skirt drops inside the walls, 4 side screws (M2.5 self-tap).
//  - Optional: Bivar PLP5-2-625 light pipes over the PWR/LNK/ACT LEDs,
//    flush-mounted, with optional debossed labels.
//  - Optional: two strap slots running across the floor, under the
//    boards, for zip ties or 12.7 mm Velcro straps (rack-rail mounting).
//  - Flat bottom (mounting tabs optional, off by default) for Command strips.
//
//  part = "base" | "lid" | "assembly" | "fit"
//         | "test_front" | "test_back" | "test_lid_leds"
//  Origin: outer front-left-bottom corner. X width, Y depth, Z up.
// =====================================================================

part = "assembly";

/* ---------------- shell ---------------- */
wall    = 2.0;
floor_t = 2.0;
lid_t   = 2.0;
fit     = 0.25;
corner_r= 3;
$fn     = 48;

/* ---------------- ESP32-POE-ISO (rev J+) ----------------
   Board-local: x across 28 mm (USB edge = 0), y = 0 at Ethernet edge */
esp_w   = 28.0;
esp_l   = 98.15;
pcb_t   = 1.6;
esp_standoff = 5.0;
esp_gap_left = 1.5;

esp_holes  = [[2.5654, 70.675], [25.4254, 70.675], [7.64, 2.73]];
pin_d      = 1.3;
standoff_d = 4.0;

rj_overhang = 8.1;
rj_cx       = 19.2;
rj_cut_w    = 17.0;
rj_cut_h    = 15.0;
rj_proud    = 1.5;      // RJ45 face beyond the outside of the front wall (0 = flush)
esp_inset   = rj_overhang - wall - rj_proud;   // PCB edge to inner face of front wall

usb_cy     = 35.8;
usb_cut_w  = 12.0;
usb_cut_h  = 8.0;
usb_cz_off = 1.5;

buttons     = [[2.17, 88.33], [25.81, 88.34]];
button_hole = 3.5;              // 0 = no button poke holes

/* ---------------- MOD-RS232 (from Olimex drawing, mils -> mm) ----------
   Board-local: u = along the board toward the DB9 (u = 31 at DB9 edge),
                v = across the board (v = 0 at the left side when the
                DB9 faces away from you).                              */
mod_size    = 30.99;            // 1220 mil square
mod_pcb_t   = 1.6;
mod_hole_in = 3.175;            // 125 mil
mod_hole_d  = 3.3;              // 130 mil
mod_gap     = 7.5;              // ESP antenna end -> MOD front edge
mod_standoff = 5.0;             // gap under the MOD PCB
mod_pin_d   = 2.8;              // locating pins in all four holes
mod_pin_standoff_d = 4.5;       // small enough to clear the DB9 lug screw heads underneath
mod_clamp_pts = [[13, 1.5], [13, 29.5]];   // [u, v]: lid posts press the board edge here
mod_clamp_d = 3.0;db9_flange_off = 1.2;           // flange face beyond the PCB edge

db9_above_pcb = 8.2;            // DB9 centre above PCB BOTTOM   <-- measure!
db9_wide_up   = true;           // D orientation                 <-- check
db9_top_w   = 16.8;
db9_bot_w   = 15.3;
db9_h       = 8.7;
db9_screw_pitch = 25.0;         // jackscrew holes (handy if yours has them)
db9_screw_d = 3.4;

/* ---------------- sizes ---------------- */
in_w = 33.0;                    // 31 mm MOD PCB / DB9 flange + clearance
//in_l = rj_overhang + 0.3 + esp_l + mod_gap + mod_size + db9_flange_off + 0.2;
in_l = esp_inset + esp_l + mod_gap + mod_size + db9_flange_off + 0.2;
in_h = 29.0;                    // +1 mm so flush light pipes clear the LEDs

W = in_w + 2*wall;
L = in_l + 2*wall;
H = floor_t + in_h;

esp_x0  = wall + esp_gap_left;
//esp_y0  = wall + rj_overhang + 0.3;
esp_y0  = wall + esp_inset;
esp_z0  = floor_t + esp_standoff;
pcb_top = esp_z0 + pcb_t;

mod_x0  = wall + (in_w - mod_size)/2;           // case x of v = 0
mod_y0  = esp_y0 + esp_l + mod_gap;             // case y of u = 0
mod_z0  = floor_t + mod_standoff;               // PCB bottom
db9_cz  = mod_z0 + db9_above_pcb;

/* ---------------- lid joint ---------------- */
skirt_d     = 4.5;
skirt_t     = 1.5;
pad_t       = 2.0;
joint_y     = [18, 70, L - 12];     // front pair moved forward, clear of the light pipes
joint_dz    = 2.5;
screw_pilot = 2.2;              // M2.5 self-tap (2.5 for M3)
screw_clear = 3.2;
screw_head  = 5.8;

/* ---------------- light pipes (Bivar PLP5-2-625) ---------------- */
light_pipes  = true;
// LEDs, board-local [x from USB edge, y from Ethernet edge, label]
// (positions read from the Olimex rev J drawing - check on your board)
pipe_leds    = [[1.3, 17.0, "PWR"], [1.3, 23.5, "LNK"], [1.3, 29.0, "ACT"]];
pipe_hole    = 3.9;             // datasheet panel hole
pipe_recess_d= 5.3;             // 5.0 mm flange + print clearance
pipe_recess  = 1.3;             // flange height -> sits flush (0 = flange on top)
pipe_body    = 20.9;            // PLP5-2-625 body length below the flange
pipe_boss_t  = 1.5;             // extra lid thickness under each pipe
pipe_boss_d  = 7.5;
led_labels   = true;            // debossed PWR / LNK / ACT next to the pipes
label_size   = 2.4;
label_depth  = 0.6;

/* ---------------- strap slots ---------------- */
strap_slots = true;
strap_w     = 13.5;             // fits 12.77 mm Velcro or ~4.8 mm zip ties
strap_h     = 1.8;
// slot centres (case Y): ESP mid-board clear zone, MOD-RS232 middle band
strap_y     = [esp_y0 + 52, mod_y0 + 13.75];
strap_ribs  = true;             // low guide ribs either side of each path
rib_h       = 1.0;
rib_w       = 1.2;

/* ---------------- options ---------------- */
mount_ears = false;             // screw tabs, two per side
ear_hole   = 4.5;
side_vents = true;
vent_y0    = 74;                // side vents start here (behind strap slot 1)
vent_n     = 6;
lid_text       = "RNET Bridge";        // line 1 ("" = none)
lid_text2      = "CAA6.6";        // line 2, optional
lid_text_size  = 6;         // letter height, mm
lid_text_y     = 74;        // centre of the text, mm back from the front edge
lid_text_depth = 0.6;       // how deep the letters are pressed in
lid_text_font  = "Liberation Sans:style=Bold";

/* ---------------- button plungers ---------------- */
btn_guides    = true;
btn_switch_h  = 2.5;    // ESP PCB top to top of the button actuator   <-- measure
btn_tube_od   = 5.4;
btn_tube_id   = 3.4;    // plunger slides in this
btn_tube_gap  = 4.0;    // tube ends this far above the switch top (room for collar)
plunger_d     = 3.0;
plunger_tip_d = 2.0;    // no wider than the actuator                  <-- measure
collar_d      = 4.8;    // wider than the tube bore, so the plunger can't escape upward
collar_t      = 1.0;
cap_proud     = 1.0;    // how far the plunger sticks up above the lid
// ---------------------------------------------------------------------


btn_z_sw  = pcb_top + btn_switch_h - H;   // switch top, in lid coordinates (lid underside = 0)
btn_tube_z = btn_z_sw + btn_tube_gap;     // bottom end of each guide tube

function esp(p) = [esp_x0 + p[0], esp_y0 + p[1]];
function mod(u, v) = [mod_x0 + v, mod_y0 + u];
mod_holes_screw = [];           // no screws: all four holes are locating pins
mod_holes_pin   = [[mod_hole_in, mod_hole_in], [mod_hole_in, mod_size - mod_hole_in],
                   [mod_size - mod_hole_in, mod_hole_in],
                   [mod_size - mod_hole_in, mod_size - mod_hole_in]];

pipe_pts   = [for (p = pipe_leds) esp([p[0], p[1]])];
pipe_ymin  = min([for (p = pipe_pts) p[1]]);
pipe_ymax  = max([for (p = pipe_pts) p[1]]);
pipe_bottom_z = H + lid_t - pipe_recess - pipe_body;

// lid vents over the PoE supply move right to make room for pipes/labels
lid_vent_x0 = !light_pipes ? esp_x0 + 4 : (led_labels ? 16.5 : 10.5);
lid_vent_x1 = W - wall - 4;

echo(str("Case outside: ", W, " x ", L, " x ", H + lid_t, " mm"));
if (light_pipes)
    echo(str("Light pipe end to PCB top: ", pipe_bottom_z - pcb_top,
             " mm (about ", pipe_bottom_z - pcb_top - 0.8, " mm above a 0.8 mm LED)"));

module rbox(s, r=corner_r) {
    hull() for (x=[r, s[0]-r], y=[r, s[1]-r])
        translate([x, y, 0]) cylinder(r=r, h=s[2]);
}

module d_shape(h_extrude) {
    tw = db9_wide_up ? db9_top_w : db9_bot_w;
    bw = db9_wide_up ? db9_bot_w : db9_top_w;
    r = 1.5;
    rotate([90,0,0]) linear_extrude(h_extrude, center=true)
        offset(r) offset(-r)
            polygon([[-bw/2,-db9_h/2],[bw/2,-db9_h/2],[tw/2,db9_h/2],[-tw/2,db9_h/2]]);
}

/* ===================== BASE ===================== */
module strap_cut(c) {
    // straight slot through both side walls, just above the floor
    translate([-1, c - strap_w/2, floor_t]) cube([W + 2, strap_w, strap_h]);
    // small flare at each entrance (up and sideways, never into the floor)
    for (s = [0, 1]) translate([s ? W : 0, c, floor_t]) mirror([s ? 1 : 0, 0, 0])
        hull() {
            translate([-1, -strap_w/2 - 0.8, 0]) cube([1, strap_w + 1.6, strap_h + 0.8]);
            translate([0.9, -strap_w/2, 0]) cube([0.1, strap_w, strap_h]);
        }
}

module base() {
    difference() {
        union() {
            difference() {
                rbox([W, L, H]);
                translate([wall, wall, floor_t]) rbox([in_w, in_l, H], 1);
            }
            // ESP standoffs + pins
            for (h = esp_holes) translate([each esp(h), 0]) {
                cylinder(d=standoff_d, h=esp_z0);
                cylinder(d=pin_d, h=esp_z0 + pcb_t - 0.1);
            }
            // MOD standoffs
            for (h = mod_holes_screw) translate([each mod(h[0], h[1]), 0])
                cylinder(d=6, h=mod_z0);
            for (h = mod_holes_pin) translate([each mod(h[0], h[1]), 0]) {
                cylinder(d=mod_pin_standoff_d, h=mod_z0);
                //. cylinder(d=mod_pin_d, h=mod_z0 + mod_pcb_t - 0.1);
                cylinder(d=mod_pin_d, h=mod_z0 + 0.8);
            }
            // strap guide ribs
            if (strap_slots && strap_ribs) for (c = strap_y, s = [-1, 1])
                translate([wall, c + s*(strap_w/2 + rib_w/2) - rib_w/2, floor_t - 0.01])
                    cube([in_w, rib_w, rib_h + 0.01]);
            if (mount_ears) ears();
        }
        // M3 pilots in the MOD standoffs
        for (h = mod_holes_screw) translate([each mod(h[0], h[1]), floor_t])
            cylinder(d=mod_screw_pilot, h=mod_z0);
        // lid screw holes (with counterbores)
        for (y = joint_y, s = [0, 1]) translate([s ? W : 0, y, H - joint_dz])
            rotate([0, s ? -90 : 90, 0]) translate([0, 0, -1]) {
                cylinder(d=screw_clear, h=wall + 2);
                cylinder(d=screw_head, h=1.8);
            }
        // RJ45 (front)
        translate([esp_x0 + rj_cx - rj_cut_w/2, -1, pcb_top - 0.5])
            cube([rj_cut_w, wall + 2, rj_cut_h]);
        // micro-USB (left)
        translate([-1, esp_y0 + usb_cy - usb_cut_w/2, pcb_top + usb_cz_off - usb_cut_h/2])
            cube([wall + 2, usb_cut_w, usb_cut_h]);
        // DB9 (back)
        translate([mod_x0 + mod_size/2, L - wall/2, db9_cz]) {
            d_shape(wall + 2);
            for (s = [-1, 1]) translate([s*db9_screw_pitch/2, 0, 0])
                rotate([90, 0, 0]) cylinder(d=db9_screw_d, h=wall + 2, center=true);
        }
        // strap slots
        if (strap_slots) for (c = strap_y) strap_cut(c);
        // vents, both long walls
        if (side_vents) for (s = [0, 1], i = [0:vent_n - 1])
            translate([s ? W - wall - 1 : -1, vent_y0 + i*8, floor_t + 4])
                cube([wall + 2, 3.5, in_h - 11]);
    }
}

module ears() {
    for (s = [0, 1], y = [22, L - 22])
        translate([s ? W : 0, y, 0]) mirror([s ? 1 : 0, 0, 0])
        difference() {
            hull() {
                translate([-10, -6, 0]) cylinder(r=1.5, h=floor_t + 1);
                translate([-10,  6, 0]) cylinder(r=1.5, h=floor_t + 1);
                translate([0, -7.5, 0]) cube([1, 15, floor_t + 1]);
            }
            translate([-6, 0, -1]) cylinder(d=ear_hole, h=10);
        }
}

// letters for the lid text: used to cut the recess and to make the inlay
module lid_text_shape(extra = 0) {
    lines = [for (t = [lid_text, lid_text2]) if (t != "") t];
    if (len(lines) > 0)
        translate([W/2, lid_text_y, lid_t - lid_text_depth]) rotate(90)
            linear_extrude(lid_text_depth + extra)
                for (i = [0:len(lines) - 1])
                    translate([10, ((len(lines) - 1)/2 - i) * lid_text_size * 1.4])
                        text(lines[i], size=lid_text_size, halign="center",
                             valign="center", font=lid_text_font);
}

// PWR / LNK / ACT labels next to the light pipes
module led_label_shape(extra = 0) {
    if (light_pipes && led_labels) for (i = [0:len(pipe_leds) - 1])
        translate([pipe_pts[i][0] + pipe_recess_d/2 + 1.0, pipe_pts[i][1], lid_t - label_depth])
            linear_extrude(label_depth + extra)
                text(pipe_leds[i][2], size=label_size, halign="left", valign="center",
                     font="Liberation Sans:style=Bold");
}

/* ===================== LID ===================== */
module lid() {
    post_len = H - pcb_top;
    preload  = 0.2;
    difference() {
        union() {
            rbox([W, L, lid_t]);
            // skirt on the long sides (left one notched for the light pipes)
            for (s = [0, 1]) {
                x = s ? W - wall - fit - skirt_t : wall + fit;
                difference() {
                    translate([x, wall + 8, -skirt_d]) cube([skirt_t, in_l - 16, skirt_d]);
                    if (light_pipes && s == 0)
                        translate([x - 1, pipe_ymin - pipe_boss_d/2 - 0.5, -skirt_d - 1])
                            cube([skirt_t + 2, pipe_ymax - pipe_ymin + pipe_boss_d + 1, skirt_d + 1]);
                }
                for (y = joint_y) {
                    px = s ? W - wall - fit - skirt_t - pad_t : wall + fit;
                    translate([px, y - 4, -skirt_d]) cube([skirt_t + pad_t, 8, skirt_d]);
                }
            }
            // ESP clamp posts
            for (h = esp_holes) translate([each esp(h), -pipe_boss_t*0 - post_len - preload])
                cylinder(d=3.6, h=post_len + preload);
            // MOD-RS232 clamp posts (press the board edges down onto its pins)
            for (c = mod_clamp_pts)
                translate([each mod(c[0], c[1]), -(H - mod_z0 - mod_pcb_t) - preload])
                    cylinder(d=mod_clamp_d, h=H - mod_z0 - mod_pcb_t + preload);
            // guide tubes for the button plungers
            if (btn_guides) for (b = buttons) translate([each esp(b), btn_tube_z])
                cylinder(d=btn_tube_od, h=-btn_tube_z + 0.01);
            // thicker lid under the light pipes (press-fit grip)
            if (light_pipes) intersection() {
                hull() for (p = pipe_pts) translate([p[0], p[1], -pipe_boss_t])
                    cylinder(d=pipe_boss_d, h=pipe_boss_t + 0.01);
                translate([wall + fit, 0, -pipe_boss_t - 1]) cube([in_w - 2*fit, L, pipe_boss_t + 2]);
            }
        }
        for (y = joint_y) translate([-1, y, -joint_dz]) rotate([0, 90, 0])
            cylinder(d=screw_pilot, h=W + 2);
        for (h = esp_holes) translate([each esp(h), -post_len - 1.2])
            cylinder(d=pin_d + 0.6, h=2.5);
        if (button_hole > 0)
            for (b = buttons) translate([each esp(b), -3]) cylinder(d=button_hole, h=10);
        // bore through the guide tubes
        if (btn_guides) for (b = buttons) translate([each esp(b), btn_tube_z - 1])
            cylinder(d=btn_tube_id, h=-btn_tube_z + lid_t + 2);
        // light pipe holes + flush recesses
        if (light_pipes) for (p = pipe_pts) translate([p[0], p[1], 0]) {
            translate([0, 0, -pipe_boss_t - 1]) cylinder(d=pipe_hole, h=pipe_boss_t + lid_t + 2);
            if (pipe_recess > 0)
                translate([0, 0, lid_t - pipe_recess]) cylinder(d=pipe_recess_d, h=pipe_recess + 1);
        }
        // LED labels
        led_label_shape(1);
        // vents over the PoE DC-DC area
        for (i = [0:5]) translate([lid_vent_x0, esp_y0 + 14 + i*5, -1])
            rbox([lid_vent_x1 - lid_vent_x0, 2.2, lid_t + 2], 1.1);
        // vents over the MOD-RS232
        for (i = [0:3]) translate([wall + 6, mod_y0 + 6 + i*5, -1])
            rbox([in_w - 12, 2.2, lid_t + 2], 1.1);
        // lid text: one or two lines, centred in the clear area of the lid
        lid_text_shape(1);
    }
}

/* ===================== previews ===================== */
module pipe_shape() {           // one PLP5-2 light pipe, flange underside at z = 0
    translate([0, 0, -pipe_body]) cylinder(d=pipe_hole, h=pipe_body);
    cylinder(d=5.0, h=0.6);
    translate([0, 0, 0.6]) scale([1, 1, 0.7 / 2.5]) sphere(d=5.0);
}

module ghosts() {
    color("green", 0.6) translate([esp_x0, esp_y0, esp_z0]) cube([esp_w, esp_l, pcb_t]);
    color("silver", 0.8) translate([esp_x0 + rj_cx - 8, esp_y0 - rj_overhang, pcb_top])
        cube([16, 21.3, 13.5]);
    color("red", 0.6) translate([mod_x0, mod_y0, mod_z0]) cube([mod_size, mod_size, mod_pcb_t]);
    color("silver", 0.8) translate([mod_x0, mod_y0 + mod_size + 0.2, db9_cz - 6.25])
        cube([mod_size, 1, 12.5]);                             // DB9 flange
    color("silver", 0.8) translate([mod_x0 + 3, mod_y0 + mod_size - 4, mod_z0 + mod_pcb_t])
        cube([mod_size - 6, 4.4, db9_cz - mod_z0 + 3]);        // DB9 body
}

module slice(y0, y1) intersection() {
    children();
    translate([-20, y0, -1]) cube([W + 40, y1 - y0, H + 10]);
}

module plunger_shape() {        // tip at z = 0, cap at the top
    len   = lid_t + cap_proud - btn_z_sw;
    c_top = btn_tube_gap - 0.5;
    cylinder(d=plunger_tip_d, h=1.0);                                      // tip
    translate([0, 0, 1.0]) cylinder(d=plunger_d, h=len - 1.0);             // shaft
    translate([0, 0, c_top - collar_t]) cylinder(d=collar_d, h=collar_t);  // collar
}

if      (part == "base") base();
else if (part == "lid")  translate([0, L, lid_t]) rotate([180, 0, 0]) lid();
else if (part == "lid_inlay") translate([0, L, lid_t]) rotate([180, 0, 0]) { lid_text_shape(0); led_label_shape(0); }
else if (part == "inlay_check") translate([0, L, lid_t]) rotate([180, 0, 0]) { lid(); color("red") { lid_text_shape(0); led_label_shape(0); } }
else if (part == "fit")  { base(); ghosts(); }
else if (part == "test_front") slice(0, 16) base();
else if (part == "test_back")  translate([0, -(mod_y0 - 3), 0]) slice(mod_y0 - 3, L + 1) base();
else if (part == "test_lid_leds")      // front 50 mm of the lid, print orientation
    translate([0, 50, lid_t]) rotate([180, 0, 0]) intersection() {
        lid();
        translate([-1, -1, -30]) cube([W + 2, 51, 40]);
    }
else if (part == "plunger")        // print 2, standing on the cap end, with a brim
    translate([0, 0, lid_t + cap_proud - btn_z_sw]) mirror([0, 0, 1]) plunger_shape();
else if (part == "assembly") {
    base(); ghosts();
    translate([0, 0, H + 20]) lid();
    if (btn_guides) for (b = buttons)
        color("orange") translate([each esp(b), pcb_top + btn_switch_h]) plunger_shape();
}
// ---- pieces for the interactive viewer (assembly coordinates) ----
if (part == "lid_asm") translate([0, 0, H]) lid();
if (part == "plunger_asm") {
    if (btn_guides) for (b = buttons)
        color("orange") translate([each esp(b), pcb_top + btn_switch_h]) plunger_shape();
}
if (part == "g_esp")  translate([esp_x0, esp_y0, esp_z0]) cube([esp_w, esp_l, pcb_t]);
if (part == "g_espmod") translate([esp_x0 + 4.0, esp_y0 + esp_l - 25.5, pcb_top]) cube([18, 25.5, 3.1]);
if (part == "g_rj")   translate([esp_x0 + rj_cx - 8, esp_y0 - rj_overhang, pcb_top]) cube([16, 21.3, 13.5]);
if (part == "g_usb")  translate([esp_x0 - 1.29, esp_y0 + usb_cy - 4, pcb_top]) cube([5.7, 8, 2.8]);
if (part == "g_leds") for (p = pipe_pts) translate([p[0] - 0.8, p[1] - 0.4, pcb_top]) cube([1.6, 0.8, 0.8]);
if (part == "g_pipes") for (p = pipe_pts) translate([p[0], p[1], H + lid_t - pipe_recess]) pipe_shape();
if (part == "g_mod")  translate([mod_x0, mod_y0, mod_z0]) cube([mod_size, mod_size, mod_pcb_t]);
if (part == "g_modhdr") translate([mod_x0 + 1, mod_y0 + 0.5, mod_z0 + mod_pcb_t]) cube([20, 8.8, 9]);
if (part == "g_db9")  union() {
    translate([mod_x0, mod_y0 + mod_size + 0.2, db9_cz - 6.25]) cube([mod_size, 1, 12.5]);
    translate([mod_x0 + 3, mod_y0 + mod_size - 4.3, mod_z0 + mod_pcb_t]) cube([mod_size - 6, 4.5, db9_cz - mod_z0 + 3]);
    translate([mod_x0 + mod_size/2, mod_y0 + mod_size + 3.5, db9_cz]) d_shape(6);
}
if (part == "g_straps") for (c = strap_y)       // 12.7 mm Velcro, looped round a rail
    translate([0, c - 12.7/2, 0]) rotate([-90, 0, 0]) linear_extrude(12.7)
        mirror([0, 1, 0]) difference() {     // XZ profile of a loop under the case
            translate([-6, -12]) offset(r=3) offset(delta=-3) square([W + 12, floor_t + 13.4]);
            translate([-4.8, -10.8]) offset(r=2) offset(delta=-2) square([W + 9.6, floor_t + 11]);
        }
