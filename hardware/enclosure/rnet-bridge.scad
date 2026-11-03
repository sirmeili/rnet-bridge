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

part = "assembly"; // [assembly, base, lid, plunger, lit_boot, lid_inlay, fit, test_front, test_back, test_lid_leds]

/* [Features] */
// Light pipes for status LEDs (ACT, LNK, PWR)
light_pipes = true;
// Show the labels for the light pipes
led_labels = true;
// true = light the user button; false = normal plungers
lit_button     = true;   
// wire groove from the pocket down to the floor
wire_groove       = true;
button_guides    = true;
// Slots for velco or zip ties
strap_slots = true;
// low guide ribs either side of each path
strap_ribs  = true;      
// Ventilation on the sides
side_vents = true;  
// screw tabs, two per side
mount_ears = false;     

/* [Text & Labels] */
// line 1 ("" = none)
lid_text_line_1 = "RNET Bridge";
// line 2, optional
lid_text_line_2 = "CAA6.6";  
// letter height, mm
lid_text_size = 6;
// left edge of the text, mm back from the front edge
lid_text_position = 40;
// how deep the letters are pressed in
lid_text_depth = 0.6;
lid_text_font = "Liberation Sans:style=Bold";
// debossed PWR / LNK / ACT next to the pipes
led_label_size = 3;
led_label_depth = 0.6;
// same order as `buttons` ("" = no label)
btn_labels = ["RST", "USR"];
button_label_size = 3;
// gap between the hole edge and the text
button_label_gap  = 1.5;

/* [Shell] */
wall_thickness    = 2.0;
floor_thickness = 2.0;
lid_thickness   = 2.0;
corner_radius = 3;
$fn     = 48;
// 31 mm MOD PCB / DB9 flange + clearance
inside_width = 33.0;                    
// +1 mm so flush light pipes clear the LEDs
inside_height = 29.0;                    

/* [Fit & screws] */
fit_clearance     = 0.25;

/* [ESP32-POE-ISO (rev J+)] */
// Board-local: x across 28 mm (USB edge = 0), y = 0 at Ethernet edge
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
// RJ45 face beyond the outside of the front wall (0 = flush)
rj_proud    = 1.5;      
esp_inset   = rj_overhang - wall_thickness - rj_proud;   // PCB edge to inner face of front wall

usb_cy     = 35.8;
usb_cut_w  = 12.0;
usb_cut_h  = 8.0;
usb_cz_off = 1.5;

buttons     = [[2.17, 88.33], [25.81, 88.34]];
button_hole = 3.5;              // 0 = no button poke holes

/* [MOD-RS232 (from Olimex drawing, mils -> mm)] */
//   Board-local: u = along the board toward the DB9 (u = 31 at DB9 edge),
//                v = across the board (v = 0 at the left side when the
//                DB9 faces away from you).

mod_size    = 30.99;            // 1220 mil square
mod_pcb_t   = 1.6;
mod_hole_in = 3.175;            // 125 mil
mod_hole_d  = 3.3;              // 130 mil
// ESP antenna end -> MOD front edge
mod_gap     = 7.5;
// gap under the MOD PCB
mod_standoff = 5.0;
// locating pins in all four holes
mod_pin_d   = 2.8;
// small enough to clear the DB9 lug screw heads underneath
mod_pin_standoff_d = 4.5;
// [u, v]: lid posts press the board edge here
mod_clamp_pts = [[13, 1.5], [13, 29.5]];
mod_clamp_d = 3.0;
// flange face beyond the PCB edge
db9_flange_off = 1.2;

// DB9 centre above PCB BOTTOM   <-- measure!
db9_above_pcb = 8.2;            
// D orientation                 <-- check
db9_wide_up   = true;           
db9_top_width   = 16.8;
db9_bot_width   = 15.3;
db9_height       = 8.7;
// jackscrew holes (handy if yours has them)
db9_screw_pitch = 25.0;         
db9_screw_diameter = 3.4;


/* [Lid Joint] */
skirt_depth     = 4.5;
skirt_thickness     = 1.5;
screw_pad_thickness       = 2.0;
screw_drop    = 2.5;
// M2.5 self-tap (2.5 for M3)
screw_pilot_hole = 2.2;
// head recess in the wall (diameter, depth)
screw_clearance_hole = 3.2;
screw_head_diameter  = 5.8;
screw_head_recess    = 0.8;
// the screws themselves (model only, for the assembly / viewer)
screw_size           = 2.5;   // nominal thread diameter (M2.5)
screw_length         = 5;     // under-head length
screw_head_style     = "button";  // "button" (hex socket) or "pan" (Phillips)
screw_head_model_diameter = 4.7;  // M2.5 button head 4.7, pan head 5.0
screw_head_height    = 1.3;   // M2.5 button head 1.3, pan head 1.5
screw_hex_socket     = 1.5;   // hex key size for button heads
// extra post length for squeeze (ESP posts use 0.2)
rs232_clamp_preload = -0.2;


/* [Light Pipes (Bivar PLP5-2-625)] */
// LEDs, board-local [x from USB edge, y from Ethernet edge, label]
// (positions read from the Olimex rev J drawing - check on your board)
pipe_leds = [[1.3, 17.0, "PWR"], [1.3, 23.0, "LNK"], [1.3, 29.0, "ACT"]];
// datasheet panel hole
pipe_hole_diameter = 4.1;
// 5.0 mm flange + print clearance
pipe_flange_recess_diameter = 5.3;
// flange height -> sits flush (0 = flange on top)
pipe_flange_recess_depth  = 1.3;
// PLP5-2-625 body length below the flange
pipe_body_length = 20.9;
// extra lid thickness under each pipe
pipe_boss_height = 5;
pipe_boss_diameter = 7.5;

/* [Strap Slots] */
// fits 12.77 mm Velcro or ~4.8 mm zip ties
strap_width = 13.5;
strap_height = 1.8;
rib_height       = 1.0;
rib_width       = 1.2;

/* [options] */
ear_hole_diameter = 4.5;
// side vents start here (behind strap slot 1)
vent_start = 74;
vent_count = 8;

/* [button plungers] */
// ESP PCB top to top of the button actuator   <-- measure
button_switch_height  = 2.5;    
button_tube_outer_diameter   = 5.4;
// plunger slides in this
button_tube_inner_diameter   = 3.4;    
// tube ends this far above the switch top (room for collar)
button_tube_gap  = 4.0;    
plunger_diameter     = 3.0;
// no wider than the actuator                  <-- measure
plunger_tip_diameter = 2.0;    
// wider than the tube bore, so the plunger can't escape upward
plunger_collar_diameter      = 4.8;    
plunger_collar_thickness      = 1.0;
// how far the plunger sticks up above the lid
plunger_height_above_lid     = 1.5;    

/* [lit button (light pipe plunger + wall-mounted LED)] */
// 1 = user button (the only one with room for a wall LED)
lit_button_number  = 1;      
// guide bore for the 3.9 mm pipe body
lit_bore_diameter       = 4.2;    
lit_tube_outer_diameter    = 6.6;
// tube end above the switch (taller than normal: deeper boot)
lit_tube_gap   = 6.0;    
// recess for the 5.0 mm flange
lit_flange_diameter   = 5.6;    
// recess depth; flange is 1.3 tall, so it sits ~1.3 proud
lit_flange_recess = 0.5;    
// gap under the flange at rest, so a press can click
lit_press_travel     = 0.5;    
// boot socket for the pipe end (glue it in)
lit_boot_socket_diameter     = 4.0;    
// boot rim, wider than lit_bore_diameter so the pipe can't escape
lit_boot_rim_diameter  = 6.0;    
// boot floor under the pipe end
lit_boot_floor      = 0.8;    

// Bambu Lab WS2812 RGB LED (KB003), set into the inside of the right wall
// width, height, thickness (from Bambu's STL)
lit_led_board    = [6.0, 6.0, 2.91];  
// LED centre height above the bottom of the case
lit_led_height   = 23.5;   
// pocket depth into the 2 mm wall (leaves 0.6 mm)
led_pocket_depth = 1.4;    
// extra room around the board
led_pocket_clearance = 0.2;    
// window in the guide tube, facing the LED
led_window_diameter     = 3.0;    
wire_groove_width     = 2.0;
wire_groove_depth     = 1.0;
// groove centre offset from the LED (lands between two vents)
wire_groove_offset     = 1.2;    
// e.g. "WS2812_RGB_LED_with_IDC0.8_4PIN-KB003.stl" to preview the real board
led_preview_stl      = "";     
// ---------------------------------------------------------------------

/* [Hidden] */
// Hidden size expressions
in_l = esp_inset + esp_l + mod_gap + mod_size + db9_flange_off + 0.2;

W = inside_width + 2*wall_thickness;
L = in_l + 2*wall_thickness;
H = floor_thickness + inside_height;

esp_x0  = wall_thickness + esp_gap_left;
esp_y0  = wall_thickness + esp_inset;
esp_z0  = floor_thickness + esp_standoff;
pcb_top = esp_z0 + pcb_t;

mod_x0  = wall_thickness + (inside_width - mod_size)/2;           // case x of v = 0
mod_y0  = esp_y0 + esp_l + mod_gap;             // case y of u = 0
mod_z0  = floor_thickness + mod_standoff;               // PCB bottom

db9_cz  = mod_z0 + db9_above_pcb;


// hidden strap slot expressions
// slot centres (case Y): ESP mid-board clear zone, MOD-RS232 middle band
strap_y     = [esp_y0 + 52, mod_y0 + 13.75];

// Hidden lid join expressions
joint_y     = [14, 70, L - 12];     // front pair moved forward, clear of the light pipes


btn_z_sw  = pcb_top + button_switch_height - H;   // switch top, in lid coordinates (lid underside = 0)
btn_tube_z = btn_z_sw + button_tube_gap;     // bottom end of each guide tube

lit_pipe_bottom = btn_z_sw + 1.0 + lit_boot_floor;                     // pipe end, lid coords
lit_pipe_len    = (lid_thickness - lit_flange_recess + lit_press_travel) - lit_pipe_bottom;

// where the LED pocket goes (case coords)
lit_p     = [esp_x0 + buttons[lit_button_number][0], esp_y0 + buttons[lit_button_number][1]];
lit_pk_w  = lit_led_board[0] + 2*led_pocket_clearance;   // pocket size along the wall (Y)
lit_pk_h  = lit_led_board[1] + 2*led_pocket_clearance;   // pocket height (Z)
lit_pk_y0 = lit_p[1] - lit_pk_w/2;
lit_pk_z0 = lit_led_height - lit_pk_h/2;
lit_gap   = (W - wall_thickness + led_pocket_depth - lit_led_board[2]) - (lit_p[0] + lit_tube_outer_diameter/2);
if (lit_button) {
    assert(lit_p[0] > W/2, "the wall LED only fits beside the user button (right wall)");
    echo(str("Lit button: cut the light pipe body to ", lit_pipe_len, " mm"));
    echo(str("Lit button: LED board to guide tube clearance ", lit_gap, " mm"));
}

function esp(p) = [esp_x0 + p[0], esp_y0 + p[1]];
function mod(u, v) = [mod_x0 + v, mod_y0 + u];
mod_holes_screw = [];           // no screws: all four holes are locating pins
mod_holes_pin   = [[mod_hole_in, mod_hole_in], [mod_hole_in, mod_size - mod_hole_in],
                   [mod_size - mod_hole_in, mod_hole_in],
                   [mod_size - mod_hole_in, mod_size - mod_hole_in]];

pipe_pts   = [for (p = pipe_leds) esp([p[0], p[1]])];
pipe_ymin  = min([for (p = pipe_pts) p[1]]);
pipe_ymax  = max([for (p = pipe_pts) p[1]]);
pipe_bottom_z = H + lid_thickness - pipe_flange_recess_depth - pipe_body_length;

// lid vents over the PoE supply move right to make room for pipes/labels
// lid_vent_x0 = !light_pipes ? esp_x0 + 4 : (led_labels ? 16.5 : 10.5);
label_x0    = pipe_pts[0][0] + pipe_flange_recess_diameter/2 + 1;   // where the labels start
lid_vent_x0 = !light_pipes ? esp_x0 + 5
            : (led_labels ? label_x0 + led_label_size * 2.4 + 3.0 : 10.5);
lid_vent_x1 = W - wall_thickness - 5;

echo(str("Case outside: ", W, " x ", L, " x ", H + lid_thickness, " mm"));
if (light_pipes)
    echo(str("Light pipe end to PCB top: ", pipe_bottom_z - pcb_top,
             " mm (about ", pipe_bottom_z - pcb_top - 0.8, " mm above a 0.8 mm LED)"));

module rbox(s, r=corner_radius) {
    hull() for (x=[r, s[0]-r], y=[r, s[1]-r])
        translate([x, y, 0]) cylinder(r=r, h=s[2]);
}

// like rbox, but rounded in the YZ plane: for slots cut through the side walls
// s = [thickness (X), width (Y), height (Z)]
module rbox_x(s, r) {
    hull() for (y = [r, s[1] - r], z = [r, s[2] - r])
        translate([0, y, z]) rotate([0, 90, 0]) cylinder(r=r, h=s[0]);
}

module d_shape(h_extrude) {
    tw = db9_wide_up ? db9_top_width : db9_bot_width;
    bw = db9_wide_up ? db9_bot_width : db9_top_width;
    r = 1.5;
    rotate([90,0,0]) linear_extrude(h_extrude, center=true)
        offset(r) offset(-r)
            polygon([[-bw/2,-db9_height/2],[bw/2,-db9_height/2],[tw/2,db9_height/2],[-tw/2,db9_height/2]]);
}

/* ===================== BASE ===================== */
module strap_cut(c) {
    // straight slot through both side walls, just above the floor
    translate([-1, c - strap_width/2, floor_thickness]) cube([W + 2, strap_width, strap_height]);
    // small flare at each entrance (up and sideways, never into the floor)
    for (s = [0, 1]) translate([s ? W : 0, c, floor_thickness]) mirror([s ? 1 : 0, 0, 0])
        hull() {
            translate([-1, -strap_width/2 - 0.8, 0]) cube([1, strap_width + 1.6, strap_height + 0.8]);
            translate([0.9, -strap_width/2, 0]) cube([0.1, strap_width, strap_height]);
        }
}

module base() {
    difference() {
        union() {
            difference() {
                rbox([W, L, H]);
                translate([wall_thickness, wall_thickness, floor_thickness]) rbox([inside_width, in_l, H], 1);
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
                translate([wall_thickness, c + s*(strap_width/2 + rib_width/2) - rib_width/2, floor_thickness - 0.01])
                    cube([inside_width, rib_width, rib_height + 0.01]);
            if (mount_ears) ears();
        }
        // M3 pilots in the MOD standoffs
        for (h = mod_holes_screw) translate([each mod(h[0], h[1]), floor_thickness])
            cylinder(d=mod_screw_pilot_hole, h=mod_z0);
        // lid screw holes (with counterbores)
        for (y = joint_y, s = [0, 1]) translate([s ? W : 0, y, H - screw_drop])
            rotate([0, s ? -90 : 90, 0]) translate([0, 0, -1]) {
                cylinder(d=screw_clearance_hole, h=wall_thickness + 2);
                cylinder(d=screw_head_diameter, h=1 + screw_head_recess);
            }
        // RJ45 (front)
        translate([esp_x0 + rj_cx - rj_cut_w/2, -1, pcb_top - 0.5])
            cube([rj_cut_w, wall_thickness + 2, rj_cut_h]);
        // micro-USB (left)
        translate([-1, esp_y0 + usb_cy - usb_cut_w/2, pcb_top + usb_cz_off - usb_cut_h/2])
            cube([wall_thickness + 2, usb_cut_w, usb_cut_h]);
        // DB9 (back)
        translate([mod_x0 + mod_size/2, L - wall_thickness/2, db9_cz]) {
            d_shape(wall_thickness + 2);
            for (s = [-1, 1]) translate([s*db9_screw_pitch/2, 0, 0])
                rotate([90, 0, 0]) cylinder(d=db9_screw_diameter, h=wall_thickness + 2, center=true);
        }
        // strap slots
        if (strap_slots) for (c = strap_y) strap_cut(c);
        if (side_vents) for (s = [0, 1], i = [0:vent_count - 1]) {
            vy    = vent_start + i*6;
            short = lit_button && s == 1 && vy + 2.2 > lit_pk_y0 - 0.5
                                         && vy < lit_pk_y0 + lit_pk_w + 0.5;
            vh    = short ? lit_pk_z0 - 1 - (floor_thickness + 4) : inside_height - 11;
            translate([s ? W - wall_thickness - 1 : -1, vy, floor_thickness + 4])
                rbox_x([wall_thickness + 2, 2.2, vh], 1.1);
        }
        if (lit_button) {
            translate([W - wall_thickness - 0.01, lit_pk_y0, lit_pk_z0])
                cube([led_pocket_depth + 0.01, lit_pk_w, lit_pk_h]);
            if (wire_groove)
                translate([W - wall_thickness - 0.01, lit_p[1] + wire_groove_offset - wire_groove_width/2, floor_thickness])
                    cube([wire_groove_depth + 0.01, wire_groove_width, lit_pk_z0 - floor_thickness + 0.01]);
        }
    }
}

module ears() {
    for (s = [0, 1], y = [22, L - 22])
        translate([s ? W : 0, y, 0]) mirror([s ? 1 : 0, 0, 0])
        difference() {
            hull() {
                translate([-10, -6, 0]) cylinder(r=1.5, h=floor_thickness + 1);
                translate([-10,  6, 0]) cylinder(r=1.5, h=floor_thickness + 1);
                translate([0, -7.5, 0]) cube([1, 15, floor_thickness + 1]);
            }
            translate([-6, 0, -1]) cylinder(d=ear_hole_diameter, h=10);
        }
}

// letters for the lid text: used to cut the recess and to make the inlay
module lid_text_shape(extra = 0) {
    lines = [for (t = [lid_text_line_1, lid_text_line_2]) if (t != "") t];
    if (len(lines) > 0)
        translate([W/2, lid_text_position, lid_thickness - lid_text_depth]) rotate(90)
            linear_extrude(lid_text_depth + extra)
                for (i = [0:len(lines) - 1])
                    translate([10, ((len(lines) - 1)/2 - i) * lid_text_size * 1.4])
                        text(lines[i], size=lid_text_size, halign="left",
                             valign="center", font=lid_text_font);
}

// PWR / LNK / ACT labels next to the light pipes
module led_label_shape(extra = 0) {
    if (light_pipes && led_labels) for (i = [0:len(pipe_leds) - 1])
        translate([pipe_pts[i][0] + pipe_flange_recess_diameter/2 + 0.5, pipe_pts[i][1], lid_thickness - led_label_depth])
            linear_extrude(led_label_depth + extra)
                text(pipe_leds[i][2], size=led_label_size, halign="left", valign="center",
                     font="Liberation Sans:style=Bold");
}

// labels behind the button holes, reading the same way as the lid text
module btn_label_shape(extra = 0) {
    for (i = [0:len(buttons) - 1]) if (btn_labels[i] != "") {
        p = esp(buttons[i]);
        hole_r = (lit_button && i == lit_button_number) ? lit_flange_diameter/2 : button_hole/2;
        translate([p[0], p[1] + hole_r + button_label_gap, lid_thickness - led_label_depth])
            rotate(90)
                linear_extrude(led_label_depth + extra)
                    text(btn_labels[i], size=button_label_size,
                         halign="left", valign="center",
                         font="Liberation Sans:style=Bold");
    }
}

/* ===================== LID ===================== */
module lid() {
    post_len = H - pcb_top;
    preload  = 0.2;
    difference() {
        union() {
            rbox([W, L, lid_thickness]);
            // skirt on the long sides (left one notched for the light pipes)
            for (s = [0, 1]) {
                x = s ? W - wall_thickness - fit_clearance - skirt_thickness : wall_thickness + fit_clearance;
                difference() {
                    translate([x, wall_thickness + 8, -skirt_depth]) cube([skirt_thickness, in_l - 16, skirt_depth]);
                    if (light_pipes && s == 0)
                        translate([x - 1, pipe_ymin - pipe_boss_diameter/2 - 0.5, -skirt_depth - 1])
                            cube([skirt_thickness + 2, pipe_ymax - pipe_ymin + pipe_boss_diameter + 1, skirt_depth + 1]);
                    if (lit_button && s == 1)          // room for the wall LED board
                        translate([x - 1, lit_pk_y0 - 0.5, -skirt_depth - 1])
                            cube([skirt_thickness + 2, lit_pk_w + 1, skirt_depth + 1]);
                }
                for (y = joint_y) {
                    px = s ? W - wall_thickness - fit_clearance - skirt_thickness - screw_pad_thickness : wall_thickness + fit_clearance;
                    translate([px, y - 4, -skirt_depth]) cube([skirt_thickness + screw_pad_thickness, 8, skirt_depth]);
                }
            }
            // ESP clamp posts
            for (h = esp_holes) translate([each esp(h), -pipe_boss_height*0 - post_len - preload])
                cylinder(d=3.6, h=post_len + preload);
            // MOD-RS232 clamp posts (press the board edges down onto its pins)
            for (c = mod_clamp_pts)
                translate([each mod(c[0], c[1]), -(H - mod_z0 - mod_pcb_t) - rs232_clamp_preload])
                    cylinder(d=mod_clamp_d, h=H - mod_z0 - mod_pcb_t + rs232_clamp_preload);

            // guide tubes for the button plungers
            // if (button_guides) for (b = buttons) translate([each esp(b), btn_tube_z])
            //    cylinder(d=button_tube_outer_diameter, h=-btn_tube_z + 0.01);
            if (button_guides) for (i = [0:len(buttons) - 1]) {
                lit = lit_button && i == lit_button_number;
                tz  = btn_z_sw + (lit ? lit_tube_gap : button_tube_gap);
                translate([each esp(buttons[i]), tz])
                    cylinder(d = lit ? lit_tube_outer_diameter : button_tube_outer_diameter, h = -tz + 0.01);
            }

            // thicker lid under the light pipes (press-fit grip)
            if (light_pipes) intersection() {
                hull() for (p = pipe_pts) translate([p[0], p[1], -pipe_boss_height])
                    cylinder(d=pipe_boss_diameter, h=pipe_boss_height + 0.01);
                translate([wall_thickness + fit_clearance, 0, -pipe_boss_height - 1]) cube([inside_width - 2*fit_clearance, L, pipe_boss_height + 2]);
            }
        }
        for (y = joint_y) translate([-1, y, -screw_drop]) rotate([0, 90, 0])
            cylinder(d=screw_pilot_hole, h=W + 2);
        for (h = esp_holes) translate([each esp(h), -post_len - 1.2])
            cylinder(d=pin_d + 0.6, h=2.5);
        if (button_hole > 0)
            for (b = buttons) translate([each esp(b), -3]) cylinder(d=button_hole, h=10);

        // bore through the guide tubes
        //if (button_guides) for (b = buttons) translate([each esp(b), btn_tube_z - 1])
        //    cylinder(d=button_tube_inner_diameter, h=-btn_tube_z + lid_thickness + 2);
        // bores through the guide tubes (+ flange recess and LED window if lit)
        if (button_guides) for (i = [0:len(buttons) - 1]) {
            lit = lit_button && i == lit_button_number;
            tz  = btn_z_sw + (lit ? lit_tube_gap : button_tube_gap);
            p   = esp(buttons[i]);
            translate([p[0], p[1], tz - 1])
                cylinder(d = lit ? lit_bore_diameter : button_tube_inner_diameter, h = -tz + lid_thickness + 2);
            if (lit) {
                translate([p[0], p[1], lid_thickness - lit_flange_recess])
                    cylinder(d=lit_flange_diameter, h=lit_flange_recess + 1);
                translate([p[0], p[1], lit_led_height - H]) rotate([0, 90, 0])
                    cylinder(d=led_window_diameter, h=lit_tube_outer_diameter/2 + 1);   // faces the right wall
            }
        }

        // light pipe holes + flush recesses
        if (light_pipes) for (p = pipe_pts) translate([p[0], p[1], 0]) {
            translate([0, 0, -pipe_boss_height - 1]) cylinder(d=pipe_hole_diameter, h=pipe_boss_height + lid_thickness + 2);
            if (pipe_flange_recess_depth > 0)
                translate([0, 0, lid_thickness - pipe_flange_recess_depth]) cylinder(d=pipe_flange_recess_diameter, h=pipe_flange_recess_depth + 1);
        }
        // LED labels
        led_label_shape(1);
        
        // button labels
         btn_label_shape(1);

        // vents over the PoE DC-DC area
        // for (i = [0:5]) translate([lid_vent_x0, esp_y0 + 14 + i*5, -1])
        //     rbox([lid_vent_x1 - lid_vent_x0, 2.2, lid_thickness + 2], 1.1);
        // (vents behind the light pipes and labels run full width, like the back vents)
        for (i = [0:5]) {
            y     = esp_y0 + 14 + i*5;
            clear = light_pipes && y > pipe_ymax + pipe_flange_recess_diameter/2 + 1;
            x0    = clear ? wall_thickness + 5     : lid_vent_x0;
            x1    = clear ? W - wall_thickness - 5 : lid_vent_x1;
            translate([x0, y, -1]) rbox([x1 - x0, 2.2, lid_thickness + 2], 1.1);
        }

        // vents over the MOD-RS232
        for (i = [0:3]) translate([wall_thickness + 5, mod_y0 + 5 + i*5, -1])
            rbox([inside_width - 10, 2.2, lid_thickness + 2], 1.1);

        // lid text: one or two lines, centred in the clear area of the lid
        lid_text_shape(1);
    }
}

/* ===================== previews ===================== */
module pipe_shape() {           // one PLP5-2 light pipe, flange underside at z = 0
    translate([0, 0, -pipe_body_length]) cylinder(d=pipe_hole_diameter, h=pipe_body_length);
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
    len   = lid_thickness + plunger_height_above_lid - btn_z_sw;
    c_top = button_tube_gap - 0.5;
    cylinder(d=plunger_tip_diameter, h=1.0);                                      // tip
    translate([0, 0, 1.0]) cylinder(d=plunger_diameter, h=len - 1.0);             // shaft
    translate([0, 0, c_top - plunger_collar_thickness]) cylinder(d=plunger_collar_diameter, h=plunger_collar_thickness);  // collar
}

module lit_boot_shape() {        // tip on the switch at z = 0
    c_top = lit_tube_gap - 0.5;  // rim stops 0.5 mm below the tube end
    cylinder(d=plunger_tip_diameter, h=1.0);                            // tip
    translate([0, 0, 1.0]) difference() {
        cylinder(d=lit_boot_rim_diameter, h=c_top - 1.0);                // rim / collar
        translate([0, 0, lit_boot_floor]) cylinder(d=lit_boot_socket_diameter, h=c_top);   // pipe socket
    }
}

module lit_led_preview() {       // the LED board sitting in its wall pocket
    face = W - wall_thickness + led_pocket_depth - lit_led_board[2];           // board face toward the tube
    if (led_preview_stl != "")
        translate([face + lit_led_board[2]/2, lit_p[1], lit_led_height])
            rotate([0, 0, 90]) translate([-128, -128, -3]) color("dimgray") import(led_preview_stl);
    else {
        color([0.15, 0.15, 0.15]) translate([face, lit_p[1] - lit_led_board[0]/2, lit_led_height - lit_led_board[1]/2])
            cube([lit_led_board[2], lit_led_board[0], lit_led_board[1]]);
        color("white") translate([face - 0.01, lit_p[1] - 1, lit_led_height - 1]) cube([0.5, 2, 2]);
    }
}

/* ===================== LID SCREWS (model) ===================== */
// one screw along +Z: underside of the head at z = 0, thread running up to z = screw_length
module screw_shape() {
    // thread (plain shank with shallow rings so it reads as a screw)
    difference() {
        cylinder(d=screw_size, h=screw_length, $fn=24);
        for (z = [0.6 : 0.45 : screw_length - 0.3])
            translate([0, 0, z]) rotate_extrude($fn=24)
                translate([screw_size/2, 0]) circle(d=0.25, $fn=8);
    }
    // head
    difference() {
        if (screw_head_style == "pan")
            translate([0, 0, -screw_head_height]) hull() {
                cylinder(d=screw_head_model_diameter, h=screw_head_height*0.5, $fn=32);
                cylinder(d=screw_head_model_diameter*0.8, h=screw_head_height, $fn=32);
            }
        else    // button head: shallow dome
            translate([0, 0, -screw_head_height])
                intersection() {
                    cylinder(d=screw_head_model_diameter, h=screw_head_height, $fn=32);
                    translate([0, 0, screw_head_height - screw_head_model_diameter*0.9])
                        sphere(d=screw_head_model_diameter*1.8, $fn=48);
                }
        if (screw_head_style == "pan")      // Phillips cross
            for (a = [0, 90]) rotate(a)
                translate([-screw_head_model_diameter*0.3, -0.3, -screw_head_height - 0.01])
                    cube([screw_head_model_diameter*0.6, 0.6, screw_head_height*0.6]);
        else                                 // hex socket
            translate([0, 0, -screw_head_height - 0.01])
                cylinder(d=screw_hex_socket/cos(30), h=screw_head_height*0.6, $fn=6);
    }
}

// the screws for one side, in assembly position. s = 0 left wall, 1 right wall.
// Each screw's head sits on the floor of its recess, thread pointing into the case.
module lid_screws(s) {
    for (y = joint_y)
        translate([s ? W : 0, y, H - screw_drop])
            mirror([s ? 1 : 0, 0, 0])
                translate([screw_head_recess, 0, 0]) rotate([0, 90, 0]) screw_shape();
}

screw_tip_x = screw_head_recess + screw_length;   // from the outside face of the wall
screw_pad_inner = wall_thickness + fit_clearance + skirt_thickness + screw_pad_thickness;
echo(str("Lid screws: tip ", screw_tip_x, " mm in from the outside, screw pad ends at ",
         screw_pad_inner, " mm (", screw_tip_x - screw_pad_inner, " mm past the pad)"));

if      (part == "base") base();
else if (part == "lid")  translate([0, L, lid_thickness]) rotate([180, 0, 0]) lid();
else if (part == "lid_inlay") translate([0, L, lid_thickness]) rotate([180, 0, 0]) { lid_text_shape(0); led_label_shape(0); btn_label_shape(0); }
else if (part == "inlay_check") translate([0, L, lid_thickness]) rotate([180, 0, 0]) { lid(); color("red") { lid_text_shape(0); led_label_shape(0); btn_label_shape(0); } }
else if (part == "fit")  { base(); ghosts(); }
else if (part == "test_front") slice(0, 16) base();
else if (part == "test_back")  translate([0, -(mod_y0 - 3), 0]) slice(mod_y0 - 3, L + 1) base();
else if (part == "test_lid_leds")      // front 50 mm of the lid, print orientation
    translate([0, 50, lid_thickness]) rotate([180, 0, 0]) intersection() {
        lid();
        translate([-1, -1, -30]) cube([W + 2, 51, 40]);
    }
else if (part == "plunger")        // print 2, standing on the cap end, with a brim
    translate([0, 0, lid_thickness + plunger_height_above_lid - btn_z_sw]) mirror([0, 0, 1]) plunger_shape();
else if (part == "lit_boot")     // print upside down: wide end on the bed
    translate([0, 0, lit_tube_gap - 0.5]) mirror([0, 0, 1]) lit_boot_shape();
else if (part == "assembly") {
    base(); ghosts();
    translate([0, 0, H + 20]) lid();
    //if (button_guides) for (b = buttons)
    //    color("orange") translate([each esp(b), pcb_top + button_switch_height]) plunger_shape();
    if (button_guides) for (i = [0:len(buttons) - 1])
    translate([each esp(buttons[i]), pcb_top + button_switch_height]) {
        if (lit_button && i == lit_button_number) {
            color("orange") lit_boot_shape();
            color("lightblue", 0.7) translate([0, 0, 1.0 + lit_boot_floor]) {
                cylinder(d=3.9, h=lit_pipe_len);
                translate([0, 0, lit_pipe_len]) cylinder(d=5.0, h=1.3);
            }
        } else color("orange") plunger_shape();
    }
    if (lit_button) lit_led_preview();
    color("silver") { lid_screws(0); lid_screws(1); }
}
// ---- pieces for the interactive viewer (assembly coordinates) ----
if (part == "lid_asm") translate([0, 0, H]) lid();
    
if (part == "plunger_asm") {if (button_guides) for (i = [0:len(buttons) - 1])
    translate([each esp(buttons[i]), pcb_top + button_switch_height]) {
        if (lit_button && i == lit_button_number) {
            color("orange") lit_boot_shape();
        } else color("orange") plunger_shape();
    }
}
if (part == "lit_button_plunger_asm") {
    if (lit_button) {
        translate([each esp(buttons[1]), pcb_top + button_switch_height]) {
            color("lightblue", 0.7) translate([0, 0, 1.0 + lit_boot_floor]) {
                cylinder(d=3.9, h=lit_pipe_len);
                translate([0, 0, lit_pipe_len]) cylinder(d=5.0, h=1.3);
            }
        }
    }
}
if (part == "screws_left_asm")  lid_screws(0);
if (part == "screws_right_asm") lid_screws(1);
if (part == "lit_button_led_asm") {
    if (lit_button) lit_led_preview();
}
if (part == "g_esp")  translate([esp_x0, esp_y0, esp_z0]) cube([esp_w, esp_l, pcb_t]);
if (part == "g_espmod") translate([esp_x0 + 4.0, esp_y0 + esp_l - 25.5, pcb_top]) cube([18, 25.5, 3.1]);
if (part == "g_rj")   translate([esp_x0 + rj_cx - 8, esp_y0 - rj_overhang, pcb_top]) cube([16, 21.3, 13.5]);
if (part == "g_usb")  translate([esp_x0 - 1.29, esp_y0 + usb_cy - 4, pcb_top]) cube([5.7, 8, 2.8]);
if (part == "g_leds") for (p = pipe_pts) translate([p[0] - 0.8, p[1] - 0.4, pcb_top]) cube([1.6, 0.8, 0.8]);
if (part == "g_pipes") for (p = pipe_pts) translate([p[0], p[1], H + lid_thickness - pipe_flange_recess_depth]) pipe_shape();
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
            translate([-6, -12]) offset(r=3) offset(delta=-3) square([W + 12, floor_thickness + 13.4]);
            translate([-4.8, -10.8]) offset(r=2) offset(delta=-2) square([W + 9.6, floor_thickness + 11]);

        }
