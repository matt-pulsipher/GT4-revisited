#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): the driver's input, phase 5 (0x349FD0): the frame the
// controller gives (or a recorded one), its steering prefilter, the HUD's bytes, the pad's controls (0x349418: the
// steering command 0x348850 and its follower 0x3485D0, a two-wheeler's lean, the throttle, brake and third axis by
// digital or analog axes and the pedal styles, the clutch 0x3481D8, the one-speed box 0x348368 / 0x348428, the
// stepless creep), the brakes onto the wheels, the nitrous, the pit and B-spec choices, and the race start's tables
// (0x347A00).

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_steer.h"

#include <functional>
#include <string>

namespace gt4r::sim {
struct Extra;
}

namespace gt4r::sim::rules {

// ---- the frame -------------------------------------------------------------------------------------------------

// The driver's frame (0x20 bytes), as a controller (RaceInput, virtual +0x28 of the table at controller + 0xD0: a player's
// controller fills it from the pad, every other's leaves it cleared) or the recorder (0x345D70) hands it to the phase.
struct PadFrame {
    u8 flags = 0;              // +0x00: bit 0 the steering analog, 1 the throttle, 2 the brake, 3 the back axis, 4 the third axis
    u8 kind = 0;               // +0x01 the controller: 0 digital, 2, 3 a DualShock's stick, 4 and 6 steering wheels
    s8 shift = 0;              // +0x02 +1 up, -1 down
    u8 count = 0;              // +0x03 the button bytes at +0x10 the request carried
    s16 steer = 0;             // +0x04 + left, +-4096 (digital: the sign)
    u8 b6 = 0;                 // +0x06 ❓
    u8 nitrous = 0;            // +0x07 not 0: the nitrous button
    u16 throttle = 0;          // +0x08 0 .. 4096 (digital: the low four bits, 1 pressed)
    u16 brake = 0;             // +0x0A
    u16 back = 0;              // +0x0C the back axis (reverse)
    u16 third = 0;             // +0x0E the third axis (the handbrake)
    std::array<u8, 16> buttons{}; // +0x10: +0x10 .. +0x13 a pit stop's answer (0x35BFE8), +0x14 / +0x15 a B-spec command (0x3F19E0)
    static PadFrame from_bytes(const u8* b); // the 32 bytes, little-endian
    void to_bytes(u8* b) const;
};

// The conductor's mailbox of a car (cond + 0xF910 + 4 i: a pit stop's answer, 4 bytes; cond + 0xF8F8 + 4 i: a B-spec
// command, 2 bytes). The phase moves them into the request it gives the controller (0x3557E8 clears it first; +0x03
// the count: 4, or 6 for a car of control source 0 / 1) and clears them; a player's controller copies the count's
// bytes into its frame's +0x10 .. (RaceInput::vf5, 0x426D50) before it reads the pad.
struct PadMail {
    std::array<u8, 4> pit{};
    std::array<u8, 2> command{};
};
// The request's part of the phase and the controller's copy: `frame`'s +0x10 .. from the mail (when `player`), the mail
// cleared (each part only when its first byte is not 0; the command's only for control source 0 / 1).
void post_mail(PadFrame& frame, PadMail& mail, s8 control, bool player);

// ---- the race's tables -----------------------------------------------------------------------------------------

// A pedal curve (16 bytes; 0x35A818 makes it from a kind and a percentage, 0x35A9D0 reads it): y = c (|x| - a)^2 + b
// (kind 1), c sqrt(|x| - a) + b (2), c x^8 + b |x| (3), |x| (others), with x's sign.
struct PedalCurve {
    u32 kind = 0;
    F a, b, c; // +0x04 / +0x08 / +0x0C
};
// 0x35A818: a pedal curve from its kind and percentage.
PedalCurve pedal_curve_of(u32 kind, s32 pct);

// The tables the input reads: what 0x347A00 builds at the race's start (the conductor's virtual 26) from the controller
// settings' bytes at 0x622110 (K below; static), and the static words beside them it reads as they are.
struct PadTables {
    // 0x620248 .. 0x620284
    F wheel_slew;                     // 0x620248: K +0x5C degrees: a steering wheel's lock bounds' slew (a second)
    std::array<F, 2> least_lock_a;    // 0x62024C / 0x620250: K +0x5D / +0x5E degrees: wheel types 1 and 3's least lock (paved, loose)
    std::array<F, 2> least_lock_b;    // 0x620254 / 0x620258 (static): wheel type 2's
    std::array<F, 3> wheel6_ratio;    // 0x62025C / 0x620260 / 0x620264: kind 6's steering per unit, by K +0x61 / +0x64 / +0x62
                                      // (900 / (60 b x 4096); 1 / 4096 for 0): P + 0x10D3 0, not 0, a loose course
    F analog_jump;                    // 0x620268: K +0x04 x 0.01: an analog axis's x + j (1 - x) once it moves
    std::array<F, 6> ramp;            // 0x620270 + 4 k: K +0x14 + k: 100 / (60 b), at most 1: a digital (or slewed) axis's
                                      // rise and fall a step: throttle up, down, brake up, down, third axis up, down
    // the controller settings' bytes read as they are
    u8 slew_analog = 0;               // K +0x11 (0x622121): an analog throttle / brake slews by the ramps (not a wheel's)
    u8 lock_by_yaw = 0;               // K +0x26 (0x622136): the lock's slip-angle extension moves with the yaw (1: not past 0)
    u8 lock_by_slip = 0;              // K +0x22 (0x622132): the extension's countersteer bound (2: not past 0)
    std::array<std::array<u8, 2>, 3> prefilter{}; // K +0x8A + 0x20 row: the steering prefilter's order and gain, by row
                                      // (0: not a kind 6; kind 6: 1 when P + 0x10D3 is 0, else 2)
    // 0x843538 ..
    std::array<PedalCurve, 3> curve;  // 0x843538 kind 6's steering (K +0x65 / +0x66), 0x843548 a wheel's throttle (+0x6C /
                                      // +0x6D), 0x843558 its brake (+0x70 / +0x71)
    F ext_yaw_gain;                   // 0x843568: K +0x27 x 0.01
    F ext_slip_gain;                  // 0x84356C: K +0x23 x 0.01
    F aid_yaw_gain;                   // 0x843570: K +0x29 x 0.01
    F aid_speed_lo, aid_speed_hi;     // 0x843574 / 0x843578: K +0x2A / +0x2B x 10 km/h (at least 10 km/h)
    u32 active_on = 0;                // 0x84357C: K +0x2C (0 when the course row's +0x12 is 2): the active steering (1: a
                                      // car whose driver byte car + 0x32 is 0, 2: every car)
    F active_gain;                    // 0x843580: K +0x2D x 0.1
    F active_damping;                 // 0x843584: K +0x2E (signed) x 0.01
    F active_max;                     // 0x843588: K +0x2F tenths of a degree
    u32 active_mode = 0;              // 0x84358C: K +0x30 (1: only with the stick centered; 2: not against the stick)
    F active_window;                  // 0x843590: K +0x31 degrees (0: a whole turn)
    // the follower (0x3485D0): row r (0 / 1), m (0 / 1): index 2 r + m
    std::array<F, 2> follow_k, follow_back, follow_slip, follow_damping; // 0x843598 / 0x8435A0 / 0x8435A8 / 0x8435B0: K +0x1C,
                                      // +0x1E, +0x20 (0 when K +0x10 is 0), +0x24 x 0.1 (+ r)
    std::array<F, 4> follow_rate, follow_rate_back, follow_rate_slip; // 0x8435B8 / 0x8435C8 / 0x8435D8: K +0x3C / +0x40 /
                                      // +0x44 (+ 2 r + m) degrees
    std::array<u32, 2> follow_by_speed{}; // 0x8435E8: K +0x36 < K +0x38 (+ r)
    std::array<F, 2> follow_speed_lo, follow_speed_hi; // 0x8435F0 / 0x8435F8: K +0x36 / +0x38 (+ r) km/h
    std::array<F, 4> fast_rate, fast_rate_back, fast_rate_slip; // 0x843600 / 0x843610 / 0x843620: K +0x4C / +0x50 / +0x54
                                      // (+ 2 r + m) degrees: the rates at the high speed
    // the two-wheeler's lean (static words 0x620730 .. 0x6207A0)
    std::array<F, 4> lean_force;      // 0x620730 / 734 / 738 / 73C: the lean's, curve A's, the camber force's and the roll
                                      // rate's gains
    std::array<F, 5> lean_follow;     // 0x620748 / 74C / 750 / 754 / 758: the lean follower's spring, damping, return,
                                      // rate limit and the steer's gain
    std::array<F, 4> lean_steer;      // 0x62078C / 790 / 794 / 798: the steering's spring (toward / away) and damping
    F lean_rate_max;                  // 0x62079C
    F lean_yaw;                       // 0x6207A0
    F special_blend;                  // 0x6207B4: a car of category 6's follower of the steering command (car + 0x77C)
    // a B-spec driver's pace levels (0x3F1790 at the race's configuration, from the bytes at 0x622088 + v (signed), 0x622090 + v,
    // 0x622098 + v; five levels)
    std::array<u8, 5> pace_shift{};   // 0x6220A8 + v: (b x 128 + 0x3200) / 100: the shift speeds' factor / 128
    std::array<F, 5> pace_brake, pace_corner; // 0x6220B0 + 4 v, 0x6220C8 + 4 v: b / 100
};
constexpr u32 kPadSettings = 0x622110, kPadWords = 0x620248, kPadTables = 0x843538;
// Built from the program's bytes as the race's start builds them. `course_road`: the conductor's byte cond + 0x10000 -
// 0x3422 (the COURSE row's +0x12, 0x34C7E0).
PadTables pad_tables(const Program& program, u8 course_road = 0);
// As the race's start left them in memory (`read(address)` a word, `byte(address)` a byte).
template <class W, class B> PadTables read_pad_tables(W&& read, B&& byte) {
    PadTables t;
    auto f = [&](u32 a) { return F::raw(read(a)); };
    t.wheel_slew = f(0x620248);
    for (u32 k = 0; k < 2; ++k) t.least_lock_a[k] = f(0x62024C + 4 * k), t.least_lock_b[k] = f(0x620254 + 4 * k);
    for (u32 k = 0; k < 3; ++k) t.wheel6_ratio[k] = f(0x62025C + 4 * k);
    t.analog_jump = f(0x620268);
    for (u32 k = 0; k < 6; ++k) t.ramp[k] = f(0x620270 + 4 * k);
    t.slew_analog = byte(kPadSettings + 0x11), t.lock_by_yaw = byte(kPadSettings + 0x26), t.lock_by_slip = byte(kPadSettings + 0x22);
    for (u32 r = 0; r < 3; ++r)
        for (u32 k = 0; k < 2; ++k) t.prefilter[r][k] = byte(kPadSettings + 0x8A + 0x20 * r + k);
    for (u32 k = 0; k < 3; ++k) {
        const u32 at = kPadTables + 0x10 * k;
        t.curve[k] = {read(at), f(at + 4), f(at + 8), f(at + 0xC)};
    }
    t.ext_yaw_gain = f(0x843568), t.ext_slip_gain = f(0x84356C), t.aid_yaw_gain = f(0x843570);
    t.aid_speed_lo = f(0x843574), t.aid_speed_hi = f(0x843578), t.active_on = read(0x84357C);
    t.active_gain = f(0x843580), t.active_damping = f(0x843584), t.active_max = f(0x843588);
    t.active_mode = read(0x84358C), t.active_window = f(0x843590);
    for (u32 r = 0; r < 2; ++r) {
        t.follow_k[r] = f(0x843598 + 4 * r), t.follow_back[r] = f(0x8435A0 + 4 * r), t.follow_slip[r] = f(0x8435A8 + 4 * r);
        t.follow_damping[r] = f(0x8435B0 + 4 * r), t.follow_by_speed[r] = read(0x8435E8 + 4 * r);
        t.follow_speed_lo[r] = f(0x8435F0 + 4 * r), t.follow_speed_hi[r] = f(0x8435F8 + 4 * r);
    }
    for (u32 k = 0; k < 4; ++k) {
        t.follow_rate[k] = f(0x8435B8 + 4 * k), t.follow_rate_back[k] = f(0x8435C8 + 4 * k), t.follow_rate_slip[k] = f(0x8435D8 + 4 * k);
        t.fast_rate[k] = f(0x843600 + 4 * k), t.fast_rate_back[k] = f(0x843610 + 4 * k), t.fast_rate_slip[k] = f(0x843620 + 4 * k);
    }
    for (u32 k = 0; k < 4; ++k) t.lean_force[k] = f(0x620730 + 4 * k), t.lean_steer[k] = f(0x62078C + 4 * k);
    for (u32 k = 0; k < 5; ++k) t.lean_follow[k] = f(0x620748 + 4 * k);
    t.lean_rate_max = f(0x62079C), t.lean_yaw = f(0x6207A0), t.special_blend = f(0x6207B4);
    for (u32 k = 0; k < 5; ++k) t.pace_shift[k] = byte(0x6220A8 + k), t.pace_brake[k] = f(0x6220B0 + 4 * k), t.pace_corner[k] = f(0x6220C8 + 4 * k);
    return t;
}
// The tables' values equal, bit for bit; `why` names the first that is not.
bool same(const PadTables& a, const PadTables& b, std::string* why = nullptr);

// ---- the record ------------------------------------------------------------------------------------------------

// What these routines keep that the engine's record has no member for. (Read too, from the other areas' parts:
// TireExtra::camber_force, BodyExtra::in_line_x480 (F + 0x480: a two-wheeler's steering rate) and in_line_x65c (F + 0x65C:
// its lean), SteerExtra::lean_row, AidExtra::assist (F + 0x4E5: bit 4 the steering's slip aid on a pad, bit 3 on a wheel),
// mark_mode (P + 0x32: the car's category), fuel_capacity, tcs_class (car + 0x81: a B-spec driver's pace), scale_on,
// PhaseExtra::pit_flags, pit_tires, soft_wheels, NearExtra::driver_line.)
struct InputExtra {
    s8 wheel_type = 0;            // F + 0x443 (car + 0x547, 0x3487F8): the steering wheel's type (1 .. 3 a least lock, 4 the
                                  // full lock straight)
    s16 steer_in = 0, steer_in_last = 0; // F + 0x454 / + 0x456: the frame's steering this step and the last (before the prefilter)
    s16 steer_seen = 0, steer_seen_last = 0; // F + 0x458 / + 0x45A: the same, as the pad's controls saw them
    F steer_view;                 // F + 0x45C: the steering as the view and the collision step's stuck nudge take it (+-1)
    u8 throttle_state = 0;        // F + 0x464: a digital or released throttle: 0 none, 1 rising, 2 rising from the back axis
    u8 steer_kind = 0;            // F + 0x465: how the command follows: 0 a wheel's (kind 4), 1 the stick's or digital, 2 a kind
                                  // 2 stick, 3 a kind 6 wheel
    F steer_rate;                 // F + 0x484: the follower's rate
    F active_last;                // F + 0x4A0: the active steering's last heading error
    u8 pit_request = 0;           // F + 0x5C1
    std::array<F, 2> line_point;  // F + 0x61C / + 0x620: a point (x, y) of the driving line by the car (the driver object's)
    F line_height;                // F + 0x624: the road's height there (0x350C20)
    u32 line_triangle = 0;        // F + 0x628: its triangle (cluster << 16 | triangle; negated when the road is not met)
    F lean_rate;                  // F + 0x660: a two-wheeler's lean's rate
    F special_steer;              // car + 0x77C (car + 0x778 + 4 x 1, 0x35A540 / 0x35A618): a category 6 car's follower of the
                                  // steering command
    u8 driver_31 = 0, driver_32 = 0; // car + 0x31 / + 0x32: the driver object's bytes +0x1D / +0x1E (the line's arguments;
                                  // the active steering's car + 0x32)
    s8 driver_40 = 0;             // car + 0x54 (driver +0x40): -1 after a B-spec car's driver takes over (0x469E48, with
                                  // driver +0x41, AidExtra::scale_on, 0)
    u8 overtake = 0;              // car + 0x82 (driver +0x6E): a B-spec driver's overtaking (its pace level: car + 0x81,
                                  // AidExtra::tcs_class; its shift factor: car + 0x83, DriveExtra::shift_factor)
    F pace_brake, pace_corner;    // car + 0x84 / + 0x88 (driver +0x70 / +0x74): its pace's braking and cornering factors
    // the parameter block
    ex::Table<6> lock_pad, lock_wheel, lock_loose; // P + 0x58 / + 0x94 / + 0xD0 (x +0xC, y +0x24): the lock by speed: a pad,
                                  // a wheel (kind 4), a loose surface (a pad on a marked surface, a wheel on a loose course)
    u8 wheel_ratio = 0;           // P + 0x10D3: a kind 6 wheel's steering ratio and its curve
};
// Every member of InputExtra with its place in Gran Turismo 4's record: v(name, index (-1: single), block, offset, member).
template <class X, class V> void input_fields(X& x, V&& v) {
    using layout::Block;
    const Block P = Block::Params, C = Block::Car;
    const u32 S = layout::kState;
    v("input.wheel_type", -1, C, S + 0x443, x.wheel_type);
    v("input.steer_in", -1, C, S + 0x454, x.steer_in), v("input.steer_in_last", -1, C, S + 0x456, x.steer_in_last);
    v("input.steer_seen", -1, C, S + 0x458, x.steer_seen), v("input.steer_seen_last", -1, C, S + 0x45A, x.steer_seen_last);
    v("input.steer_view", -1, C, S + 0x45C, x.steer_view);
    v("input.throttle_state", -1, C, S + 0x464, x.throttle_state), v("input.steer_kind", -1, C, S + 0x465, x.steer_kind);
    v("input.steer_rate", -1, C, S + 0x484, x.steer_rate), v("input.active_last", -1, C, S + 0x4A0, x.active_last);
    v("input.pit_request", -1, C, S + 0x5C1, x.pit_request);
    for (int k = 0; k < 2; ++k) v("input.line_point", k, C, S + 0x61C + 4 * u32(k), x.line_point[size_t(k)]);
    v("input.line_height", -1, C, S + 0x624, x.line_height), v("input.line_triangle", -1, C, S + 0x628, x.line_triangle);
    v("input.lean_rate", -1, C, S + 0x660, x.lean_rate), v("input.special_steer", -1, C, 0x77C, x.special_steer);
    v("input.driver_31", -1, C, 0x31, x.driver_31), v("input.driver_32", -1, C, 0x32, x.driver_32);
    v("input.driver_40", -1, C, 0x54, x.driver_40);
    v("input.overtake", -1, C, 0x82, x.overtake);
    v("input.pace_brake", -1, C, 0x84, x.pace_brake), v("input.pace_corner", -1, C, 0x88, x.pace_corner);
    auto table = [&](const char* n, u32 at, auto& t) {
        v(n, -1, P, at, t.n);
        for (int k = 0; k < 6; ++k) v(n, k, P, at + 0xC + 4 * u32(k), t.x[size_t(k)]), v(n, 6 + k, P, at + 0x24 + 4 * u32(k), t.y[size_t(k)]);
    };
    table("input.lock_pad", 0x58, x.lock_pad), table("input.lock_wheel", 0x94, x.lock_wheel), table("input.lock_loose", 0xD0, x.lock_loose);
    v("input.wheel_ratio", -1, P, 0x10D3, x.wheel_ratio);
}

// ---- the routines ----------------------------------------------------------------------------------------------

// What the race gives the phase beside the car.
struct InputRace {
    u8 course_kind = 0;        // 0x34C968: the conductor's byte cond + 0x10000 - 0x3423 (the COURSE row's +0x10: 0 dry, 1 wet,
                               // 2 dirt, 3 snow; 0xFF as 0)
    bool lan_gain = false;     // the race object's virtual +0x290 (RaceBase: 0; only a LAN battle's differs): the prefilter's gain + 2
    bool lan_average = false;  // its virtual +0x318 > 0: the prefilter takes the mean of this frame and the last
    F line_heading;            // 0x4649F0 on the line object (cond + 0xDA50) at the car's distance along the course, its line
                               // (car + 0x30, + 0x31): the active steering's aim
};
// The ground's answer at a point (0x395760: from 5 m above; the road query 0x3E5A30).
struct GroundAnswer {
    bool found = false;
    F height;           // the record's +0x10 (the point met: its height)
    u32 triangle = 0;   // the record's +0x0C (cluster << 16 | triangle)
};
// The parts of the phase that are not the car record's: each is skipped when empty.
struct InputHooks {
    std::function<void()> line_begin, line_end; // 0x465078 / 0x469E58 on the driver object (car + 0x14)
    std::function<void()> computer_clutch;      // 0x3F3400 (the computer's clutch request; the session's)
    std::function<void()> computer_outputs;     // 0x349F80: 0x464F38 (the driver's outputs into the controls), then a stopped
                                                // driver's (kind 8) handbrake 1
    std::function<GroundAnswer(const F p[3])> ground; // 0x395760 for 0x350C20 (not called: the line's fields stay)
    // a B-spec command's calls (0x3F19E0)
    std::function<bool()> no_pit;               // 0x343C20 (not 0: the pit is not open to the car; empty: open)
    std::function<void()> line_change;          // 0x463C28 (driver, 6, 0)
    std::function<void(int)> race_control;      // 0x387D38 (race object, 0 / 1)
};

// 0x3484D0 (on the state block): the HUD's steering percent F + 0x460 (digital: +-100 by the sign; analog steer x 100 / 4096,
// toward 0) and the full-throttle flag F + 0x461 cleared when the throttle is short of full (analog: not 4096; digital: 0).
void pad_hud(ex::Car& c, const PadFrame& f);
// The phase's steering prefilter (0x349FD0): with the row's order (+0x1A) not 0, the frame's steering + gain x its change
// (order 2: + gain^2 x the change's change), within +-4096; F + 0x454 / + 0x456 take the frame's own.
void steer_prefilter(ex::Car& c, InputExtra& x, const PadTables& t, const InputRace& race, PadFrame& f);
// 0x348588: x toward `target` by `step`, not past it.
void slew(F& x, F target, F step);
// 0x35A9D0: a pedal curve at x; with `slope`, its slope there.
F pedal_curve(const PedalCurve& k, F x, F* slope = nullptr);
// 0x3485D0: the steering command's follower toward `target` (row `row` 0 / 1, `back` 0 / 1 (1 unless F + 0x465 is 1)): a
// spring and damper, its pull toward the wheels' slip direction `slip` while it lies beyond the command, the return's
// spring and rates, the rates by speed, the command set on the target once it is passed.
void steer_follow(ex::Car& c, InputExtra& x, const PadTables& t, int row, int back, F target, F slip);
// 0x348850: the steering command F + 0x468 from the frame (not a two-wheeler): the lock by speed and controller, the
// slip-angle extension, a wheel's bounds, kind 6's ratio and curve, the slip aid, the active steering and the follower.
void steer_command(ex::Car& c, sim::Extra& x, const WheelLayout& l, const PadTables& t, const SteerTables& g, const InputRace& race,
                   const PadFrame& f);
// The two-wheeler's steering by the lean (0x349418's own path, wheel layout kind 1).
void lean_steer(ex::Car& c, sim::Extra& x, const PadTables& t, const SteerTables& g, const PadFrame& f);
// 0x3481D8: the clutch request F + 0x514 for an ordinary box (style: the pedal style the controls took).
void pad_clutch(ex::Car& c, const PadFrame& f, u8 style);
// 0x348368: the one-speed box's clutch request (a brake over 0.1 at a standstill opens it).
void one_speed_clutch(ex::Car& c);
// 0x348428: the one-speed box's creep (0x368548: the throttle at least 0.3 (1 - v / 4.17 m/s)) and its reverse request.
void one_speed_creep(ex::Car& c, const PadFrame& f);
// 0x349418: the pad's controls: the steering (0x348850, or the lean), the throttle and back axis by the pedal style, the
// brake, the shift, the third axis, the clutch by the gearbox's kind and the stepless box's creep.
void pad_controls(ex::Car& c, sim::Extra& x, const WheelLayout& l, const PadTables& t, const SteerTables& g, const InputRace& race,
                  const PadFrame& f);
// 0x349F48: the pad's controls, the throttle F + 0x494 = the pedal, the brake pedal onto the layout's wheels (0x343CF8).
void pad_input(ex::Car& c, sim::Extra& x, const WheelLayout& l, const PadTables& t, const SteerTables& g, const InputRace& race,
               const PadFrame& f);
// 0x35BFE8: a pit stop's answer (frame +0x10 .. +0x13: 1 the tires chosen, 2 go) into the pit flags F + 0x4B8.
void pit_answer(ex::Car& c, sim::Extra& x, const PadFrame& f);
// 0x3F19E0: a B-spec command (frame +0x14: 4 pace, 5 overtaking, 6 pit, 10 who drives; +0x15 its value).
void bspec_command(ex::Car& c, sim::Extra& x, const PadTables& t, const InputHooks& h, u8 kind, u8 value);
// 0x350C20: the road's height under the line's point (F + 0x624 / + 0x628).
void line_ground(ex::Car& c, InputExtra& x, const InputHooks& h);
// 0x348800: the steering command x 12 / pi within +-1.
F steer_unit(F steer);

// 0x349FD0, the phase, on the frame the controller (or a recording) gave: the frame's request's answers (pit,
// B-spec), the nitrous, F + 0x461, the prefilter, then by the control source F + 0x466 and driver kind F + 0x467:
// control 0 with kind 0 the pad (0x3484D0, 0x349F48 between the line's hooks), control 0 with a driver kind, 1 (after
// 0x3484D0) or 2 the computer's (0x3F3400, 0x349F80); a category 4 car's clutch at shifting (pedals off), 0x350C20, a
// category 6 car's follower, and after a computer's path F + 0x45C = 0x348800 (the command) x 0.6667.
void driver_input(ex::Car& c, sim::Extra& x, const WheelLayout& l, const PadTables& t, const SteerTables& g, const InputRace& race,
                  const InputHooks& h, PadFrame f);

} // namespace gt4r::sim::rules
