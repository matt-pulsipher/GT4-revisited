#pragma once

// Gran Turismo 4's own routines of a car's start (rules.h): the car's set-up on its start place (0x363B10: the pose
// 0x3630C0, the standing start 0x363560 and what follows it), a rolling start's places and the car put on the road
// already moving (0x363868), the rolling car's hold and its release (0x3452F0, 0x345228), the start's countdown
// (0x34C210 and each car's own hold counter in the race step 0x34DB30) and the attitude's start hold (0x36AE20).

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules_body.h"
#include "gt4r/sim/rules_drive.h"

#include <functional>

namespace gt4r::sim {
struct Extra;
class Program;
}

namespace gt4r::sim::rules {

struct WearRules;

// What the start's routines keep that no other area's part of the record has a member for.
struct StartExtra {
    u8 start_mark = 0;        // F + 0x65B: 0x363B10's mark of a car placed in a race that counts its start (0x34C3E0): 1 or 2,
                              // 1 for start kind 2; 0 otherwise ❓ its readers
    u8 line = 0;              // F + 0x4B7: the car's driving line in the line object (0x34C760 → 0x46BDA0: 8 less the car's
                              // index with lines loaded, else 0); the attitude's start hold aims along it (0x34C7C0)
    u16 rolling_steps = 0;    // F + 0x590: 180 for start kind 3, 300 for 8, else 0; cleared by the release (0x3F37B8) ❓ its readers
    u8 release_count = 0;     // F + 0x633: 30 from the standing start (0x353ED8) and from the release ❓ its readers
    u8 lap_started = 0;       // F + 0x4B0: cleared by the set-up; 1 for a car of start kind 6 past half the lap ❓
    u8 x4b4 = 0, x4b6 = 0;    // F + 0x4B4 / + 0x4B6: cleared by the set-up (0x363528 clears + 0x4B6) ❓
    u8 x5c2 = 0, x632 = 0;    // F + 0x5C2 / + 0x632: cleared by the set-up ❓ (0x35DF80 reads + 0x632: no messages when set)
    u32 x4dc = 0, x614 = 0;   // F + 0x4DC / + 0x614: cleared by the set-up ❓
    // The fuel's block as 0x354AF8 starts it (F + 0x4BD 1, + 0x4BE, + 0x4C0 the least fuel, + 0x4C4, + 0x4C8; the step's
    // fuel routines rewrite them, 0x354B78: not ported, so they have no place here; the fuel's own port places them).
    u8 fuel_on = 0;
    u16 x4be = 0;
    F fuel_least, x4c4, x4c8;
    u8 x6c4 = 0;              // car + 0x6C4 (0x3F3360) ❓
    // The parameter block's words the set-up reads that the converter's mirror has no member for (P + 0x104B, P + 0x10D8:
    // not made by our converter yet, so 0 and without a place).
    u8 tank = 0;              // P + 0x104B: the fuel tank (liters)
    F nitrous_full;           // P + 0x10D8: the nitrous a car starts with when its gain (P + 0x10D4) is above 0
};
template <class X, class V> void start_fields(X& x, V&& v) {
    using layout::Block;
    const Block C = Block::Car, P = Block::Params;
    const u32 S = layout::kState;
    v("start.mark", -1, C, S + 0x65B, x.start_mark), v("start.line", -1, C, S + 0x4B7, x.line);
    v("start.rolling_steps", -1, C, S + 0x590, x.rolling_steps), v("start.release_count", -1, C, S + 0x633, x.release_count);
    v("start.lap_started", -1, C, S + 0x4B0, x.lap_started), v("start.x4b4", -1, C, S + 0x4B4, x.x4b4);
    v("start.x4b6", -1, C, S + 0x4B6, x.x4b6), v("start.x5c2", -1, C, S + 0x5C2, x.x5c2), v("start.x632", -1, C, S + 0x632, x.x632);
    v("start.x4dc", -1, C, S + 0x4DC, x.x4dc), v("start.x614", -1, C, S + 0x614, x.x614);
    v("start.x6c4", -1, C, 0x6C4, x.x6c4);
    (void)P; // (the parameter block's tank and nitrous: the converter's, placed when it makes them)
}

// The program's words the set-up reads (its data segment and globals).
struct StartTables {
    u8 fuel_less = 0;  // 0x620E24: liters a race's fuel starts below the tank
    F motor_scale;     // 0x8438DC: layout 7's motors' store's capacity (MotorTables::capacity, which 0x355978 makes at the car's conversion)
    u32 steer_mode = 0; // 0x845BF0 (0x3F7C80): 2 puts the steer angle in the wheel's offset (wheel + 0x1C)
};
StartTables start_tables(const Program& program);

// The race's rolling start (the single-player conductor's virtual +0xF0, 0x347618): when no car has a
// place or speed of its own, the rule block's +0x6C (s16, metres from the start line, negative before it)
// is the last place's distance, +0x6F (metres) the gap between places and +0x6E the speed (km/h). The
// car at grid place `place` of `count` starts at offset + (count - place - 1) x spacing.
int rolling_start_distance(int offset, int spacing, int count, int place);

// 0x36BF68: the gear for a start at v (m/s): reverse for v < 0; first for a one-speed gearbox (kind 3)
// or a stepless one of kind 1; else the first gear g (1 .. gears - 1) whose up-shift speed less 2,000 rpm
// (first gear) or 500 rpm (the others) is above v at its speed factor; the top gear when none is.
inline int start_gear(const ex::Car& c, const DriveExtra& x, F v) { return gear_for_speed(c, x, v); }

// 0x363868: the car moving at v (m/s) along its heading, as the car's set-up leaves it for a rolling
// start: v at most the car's top speed (P +0xFD8), the yaw rate cleared, the velocity along the forward
// axis, the speed records (0x363538), the gear for v, the engine at v times the gear's speed factor
// (from idle to the rev limit; a stepless gearbox of kind 1 holds it at its hold speed), the turbo
// spooled, the clutch locked and engaged, every wheel's rim and hub speed and both axles' speeds at v.
// `computer`: a computer drives the car (0x463B98), which starts with the throttle open.
void moving_start(ex::Car& c, DriveExtra& x, F v, bool computer);

// ---- the car's set-up (0x363B10) ----------------------------------------------------------------------------------

// What the pose asks of the course (the road query 0x395760 and the conductor's virtuals).
struct PoseHooks {
    // 0x395760 on a ground record: its point (+0x00 x, + 0x04 height, + 0x08 z, the course's frame) and hint (+0x0C) in,
    // the answer out (the query itself looks from 5 m above the point); returns the wheel's surface index for the answer
    // (0x3F4E40: the material, or its wet variant).
    std::function<u8(ex::Ground& g)> query;
    // 0x3F4EA0: the record's material is a soft one (the bumps' table 0x622430).
    std::function<bool(const ex::Ground& g)> soft;
    // The conductor's virtual +0x180: a wheel on the pit lane sets the car's flag (F + 0x4B1 bit 4).
    std::function<bool()> pit_lane = [] { return true; };
    // The conductor's virtual +0x108 (not on a course of kind 2, conductor + 0x10000 - 0x3422): the car's distance along
    // the course (F + 0x4A4); null: 0.
    std::function<F(const ex::Car& c)> distance;
};

// 0x3630C0: the car at rest at (x, y, z) facing `heading` (the record's frame: x, y and z up, as F + 0x00; the place's
// height z is the road's there): the road under the point (F + 0x4A8 its triangle, the height + the center of gravity's
// height P + 0x3C the car's), pitch and roll 0, the body's axes (0x36C9F8) and hubs (0x345A20); each of the layout's
// wheels' ground record at its hub (a wheel off the road on the body's ground at its hub's height, surface 0), the
// wheels on a soft material (F + 0x62C) and on the pit lane (F + 0x62D, F + 0x4B1 bit 4); each wheel's load and peak
// force the axle's load over its wheels + the preload (P + 0x1040, + 0x344), its travel at rest (P + 0x318), steer angle
// (0x3F7CA8 at 0; with steering mode 2 its offset too), camber (0x36A578) and half track (0x36A690) at 0; the ground's
// frame (0x35D350), the attitude the ground's with the ride heights' pitch (atan2 of P + 0x40 - P + 0x44 over the
// wheelbase), the height over the ground's point along the up axis, the hubs again; the distance along the course, and
// the line's last point F + 0x61C .. + 0x628 the ground's point and triangle, F + 0x650 0.
void pose(ex::Car& c, Extra& x, const WheelLayout& l, const StartTables& t, const PoseHooks& h, s32 hint, F px, F py, F pz, F heading);
// 0x3455F0 (the car's part): the view's heading and pitch lags (F + 0x5FC / + 0x600) cleared. (It also clears the car's
// record of velocity changes in the conductor, + 0xCAB8 + 0x30 x index: not the car's.)
void clear_view_lags(ex::Car& c);
// 0x363560: the car standing: every one of the layout's wheels' forces, speeds and slips cleared (slip gain 1); the
// differentials locked by kind (P + 0x244 + axle: 3 or 4) and the center locked by the layout (4 .. 6 or 9); the time
// scale 1, the steering's bounds by the lock (P + 0x54), the controls, speeds, rates and engine cleared; the speed records
// (0x363538); first gear (0x36BEB8); the pad's feedback record (0x353EB8 / 0x353EA0, the
// conductor's), F + 0x633 30 (0x353ED8), the engine sound's levels 0x7F; each wheel's effects (0x363018); the gearbox's
// lock-out 12 for control source 1, a driver kind, or pedal style 0 or 2; the gear marks (0x35A638).
void standing_start(ex::Car& c, Extra& x, const WheelLayout& l);
// 0x354AF8 (a stand-in until the fuel's own port; the agent of the fuel owns it): the fuel's block; with the rule block's
// +0x4C (`fuel_rule`) set, the tank (P + 0x104B) the capacity and the tank less 0x620E24 (at least the least fuel) the
// fuel, else none; then the masses (0x354AC0).
void start_fuel(ex::Car& c, Extra& x, u8 fuel_rule, const StartTables& t);

// What the set-up's caller tells it (the initialisation 0x34D490 from the conductor's virtuals and the entry).
struct SetupArgs {
    int index = 0;            // the car's index (F + 0x440); its rank index + 1 (F + 0x4AF)
    u8 not_racing = 0;        // F + 0x441
    u8 entry_driver = 0;      // the entry's +0x344C (0 a player, 1 .. a computer): car + 0x547 1, 2, 3 for 0, 1, 2, else 1
    s8 control = 0;           // the control source (0x362E00: F + 0x466; F + 0x467 cleared)
    u8 pedal_style = 0;       // (0x362E10: kept for control source 0, 1 and 4; 0 for 2)
    int start_kind = 0;       // the conductor's virtual +0xF0: 0 the grid; 1 / 2 a rolling start at / before the line; 3, 4,
                              // 5 others on the line; 6 / 7 a pit start; 8 on the line held
    F speed;                  // km/h of a moving start (0: standing); start kind 4 takes the driver's own
    u8 fuel_rule = 0;         // the rule block's +0x4C
    s32 hint = -1;            // the road triangle near the place
    F x, y, z, heading;       // the place (record frame) and the car's heading (the grid place's + pi)
    u8 start_mark = 0;        // what the race's start counting (0x34C3E0) made of the place: 0, 1, 2
    u8 line = 0;              // the car's driving line (0x34C760)
};
struct SetupHooks {
    PoseHooks pose;
    // A car the computer drives on before its start (start kinds 2, 3, 4 and 8, 0x3452F0): its driver's line changes from
    // the passing line 7 to line 6. Returns false when the driver has no such line (the car is held all the same).
    std::function<void()> to_line6;
    // A pit start (start kinds 6 and 7, 0x345400): the driver onto the stop line 4 (driver kind 3 when it has the line).
    std::function<bool()> to_stop_line;
    // Start kind 4: the computer driver's own speed (m/s, its object's +0x28).
    std::function<F()> driver_speed;
    // A moving start's throttle open (0x463B98: the driver's byte +0x1E is 0); null: open.
    std::function<bool()> open_throttle;
};
// 0x363B10 from the converter's return (the parameter block made, the computer driver's object set up): the pedal style,
// the race's flags and pit words cleared; a car on the grid (start kinds 0 and 8) with its nose at the place (the
// place moved forward by P + 0x00 along the heading); F + 0x65B; the pose (0x3630C0), 0x3455F0, the standing start
// (0x363560), new tires (0x362FA8: 0x362E58 on each wheel), 0x363528, no start's speed cap (0x35FC48), the race's marks
// cleared, the fuel (start_fuel), the nitrous (P + 0x10D8 when P + 0x10D4 is above 0), 0x3F3360, a category 3 car's state
// cleared (0x355840), layout 7's motors (0x355DC8), a category 1 car's mood (0x35D108), the step length (0x3658A0); the
// hold counter and the start's hold cleared, the best lap none (0x35E3B8), the line and the rank index; a pit start held
// 120 steps (F + 0x4BA) on the stop line; a rolling start (kinds 2 .. 4, 8) held on line 6 (0x3452F0: driver kind 1) with
// the gearbox locked out 12 steps; a moving start (0x363868) at its speed; the throttle's follower (0x3F9CF0), the
// speed-switched part's state (0x3F27E8), a category 1 car's state (0x35C7E0), F + 0x634 the rpm (0x350EB0). Not here:
// the laps and sectors from the place (the race's, F + 0x4AC .. + 0x4B0 but for + 0x4B0's clearing), the driving line's
// place of a rolling start (0x34C5E0: the caller's x, y, z, heading), the driver's line start (0x4639E0), the hybrid
// box's set-up (0x3683C8: not ported).
void car_setup(ex::Car& c, Extra& x, const WheelLayout& l, const StartTables& t, const WearRules& wear, const SetupArgs& a, const SetupHooks& h);

// 0x3452F0 on the record: unless the control source is 4, the car is held in its rolling start (F + 0x467 = 1). Returns
// whether its driver is to leave the passing line for line 6 (driver kind 2 .. 4, or control source and driver kind 0,
// and the driver on line 7).
bool rolling_hold(ex::Car& c, Extra& x);
// 0x345228 with the single-player conductor's virtual 20 (+0xA0, 0x347928) and 0x3F37B8: a car held in its rolling start
// (F + 0x467 = 1) let go: its driver's line hold cleared (driver + 0x1B), then for a player's car (control source 0) its
// driver onto line 7, else for driver kinds 2 .. 4 line 6; when the line is there (`line_ok`; on a course whose byte
// conductor + 0x10000 - 0x341F is set at once) driver kind 0 and F + 0x633 30; F + 0x590 cleared. Returns whether it
// was let go.
bool release(ex::Car& c, Extra& x, bool line_ok);

// ---- the start's countdown and the attitude's hold ----------------------------------------------------------------

// The race step's start hold for one car (0x34C210 and 0x34DD60 .. 0x34DDF4): while the race's countdown runs (the race
// information's +0xBC above 0) the car is held (F + 0x4BC = 1); after it, with its own counter (conductor + 0xCBFC + 4 x
// index: 60 x the rule block's s16 +0x60 + 2 x index for a car without a speed of its own) still running it is counted
// down, held while above 0; on the step it reaches 0 the hold is left as it was (and the race told, 0x35DF80(4)); with it
// at 0 the hold is cleared. Returns true on the step the counter ran out.
bool start_hold(ex::Car& c, bool countdown, u32& counter);

// 0x36A298: the hold's height over the ground at hold step n (720 steps): from 720 none (kind 0), 660 .. 719 rising to `v`
// over 60 steps (kind 1), 60 .. 659 `v` (kind 2), below 60 none (kind 0).
F hold_lift(u32 n, F v, u32& kind);
// 0x36A318: the body lifted to the hold's heights over the ground (0x36A298 at the counter and 15 steps on) at the axles
// (P + 0x10 / + 0x14 along the forward axis): when either axle would be below its height, the body at the axles' weighted
// height (P + 0x24 / + 0x20), pitched by their difference over the wheelbase, F + 0x558 its rise x 60 / the time scale,
// roll, the vertical speed and the pitch and roll rates 0. Returns whether it lifted.
bool hold_lift_body(ex::Car& c, const BodyExtra& b, F v);
// 0x36AE20 (the attitude's start, 0x36B0C8, before anything else of it): while the hold counter F + 0x4BA runs, the body
// lifted (0x36A318 with 0.46 less the front's offset P + 0x314 less the held droop P + 0x34C) and, when it lifted, the
// heading turned toward the car's line's (`line_heading`, 0x34C7C0 → 0x46BE70: 0 without lines) at most 0.526 rad a
// second, the body's axes (0x36C9F8). Returns true when it lifted (the attitude ends there).
bool attitude_hold(ex::Car& c, const BodyExtra& b, F line_heading);

} // namespace gt4r::sim::rules
