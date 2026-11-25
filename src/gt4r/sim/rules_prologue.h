#pragma once

// Gran Turismo 4 Prologue's own race step (docs/formats/prologue.md "The race step"). Its car record is Gran Turismo 3's
// (the shared engine's mirror, sim/exact/car_record.h), its parameter block Gran Turismo 3's with each tire record's
// three kind bytes after it (Gran Turismo 4's +0x1D8 .. +0x1DA). Its step's routines are each either the engine's
// (Gran Turismo 3's, equal at every call: tools/gt4diff's Prologue taps), Gran Turismo 4's (rules_*.h) on the record's
// Gran Turismo 4 part made from Prologue's (prologue_extra), or Prologue's own (below). Addresses are SCPS-15055 v1.01's.

#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/convert_prologue.h"
#include "gt4r/sim/equipment.h"
#include "gt4r/sim/record.h"
#include "gt4r/sim/rules_chassis.h"
#include "gt4r/sim/rules_hybrid.h"
#include "gt4r/sim/rules_input.h"
#include "gt4r/sim/rules_steer.h"
#include "sim/exact/tick.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_tires.h"

#include <array>

namespace gt4r::sim {
class Program;
}

namespace gt4r::sim::prologue {

namespace ex = gt3r::sim::exact;
using gt3r::ee::F;

// The steering follower's settings (0x715240 .. 0x7152D8, Gran Turismo 4's 0x843598), by the rate class r (0 / 1) and, for
// the rates, by i = 2 r + the follower's mode (0: digital or a stick, 1: a table-mapped analog). Made by the controls' set-up
// (0x3DD068) from the controller settings' bytes C (0x504950).
struct SteerFollow {
    std::array<F, 2> gain;      // +0x00 C[0x1C + r]: the pull toward the target
    std::array<F, 2> centering; // +0x08 C[0x1E + r]: the pull toward the center while returning
    std::array<F, 2> toward;    // +0x10 C[0x20 + r] (0 when C[0x10] is 0): the pull toward the front wheels' mean slip direction
    std::array<F, 2> damping;   // +0x18 C[0x24 + r] / 10
    std::array<F, 4> rate, rate_back, rate_toward; // +0x20 / +0x30 / +0x40 C[0x34 + i] / C[0x38 + i] / C[0x3C + i] degrees a
                                                   // second: the rate's limit, returning, toward the slip direction
    std::array<u32, 2> by_speed;                   // +0x50 C[0x2E + r] < C[0x30 + r]: the limit moves with the speed
    std::array<F, 2> speed_lo, speed_hi;           // +0x58 / +0x60 C[0x2E + r] / C[0x30 + r] km/h: from where to where
    std::array<F, 4> fast, fast_back, fast_toward; // +0x68 / +0x78 / +0x88 C[0x44 + i] / C[0x48 + i] / C[0x4C + i]: the limits at
                                                   // and above speed_hi
};

// What the controls' set-up (0x3DD068, Gran Turismo 4's 0x347A00; DynamicsConductor virtual 23 at the race's start) makes
// from the controller settings' bytes C (0x504950) and what the pad's input reads beside them.
struct Controls {
    // 0x504AA8 .. 0x504ABC: 100 / (C[0x14 .. 0x19] x 60), at most 1: the throttle's up and down, the brake's, the third axis'.
    F throttle_up, throttle_down, brake_up, brake_down, third_up, third_down;
    F analog_jump;               // 0x504AA4: C[4] / 100
    F wheel_slew;                // 0x504A88: C[0x54] degrees a second, the steering wheel's bounds' slew
    std::array<F, 4> wheel_lock; // 0x504A8C / 0x504A90 C[0x55] / C[0x56] degrees, wheel types 1 and 3 off dirt / on dirt;
                                 // 0x504A94 / 0x504A98 (the image's) type 2
    std::array<F, 2> jog_scale;  // 0x504A9C / 0x504AA0 (off dirt / on dirt) the controller kind 6's steering scale: 900 /
                                 // (C[0x59] (C[0x5A]) x 60 x 4096), 1 / 4096 when 0
    SteerFollow follow;
    std::array<u8, 32> surface_class{}; // 0x504AC0 + surface: 1 loose (the follower's rate class 1)
    // The input phase's steering prefilter (0x3DEDE0) by row (0: every controller kind but 6; kind 6: 1, 2 for a car whose
    // parameter block's byte +0x33 is set): C[0x82 + 0x20 row] the mode (0 off, 1 the step's change, 2 also its change's
    // change), C[0x83 + 0x20 row] the gain.
    std::array<u8, 3> prefilter_mode{}, prefilter_gain{};
    // A steering wheel's pedals (0x715220 / 0x715230, made by 0x3C2938): the throttle's curve by C[0x64] (kind) and C[0x65]
    // (percent), the brake's by C[0x68] and C[0x69].
    rules::PedalCurve wheel_throttle, wheel_brake;
};
// The set-up on the controller settings' bytes from 0x504950 (0x190 bytes: through the surface classes).
constexpr u32 kControlBytes = 0x190;
Controls controls(const u8* settings);

// Prologue's tables the step reads, from its program (program_prologue.h places them).
struct Tables {
    bool found = false;                // the program is Prologue's
    rules::SlipBytes slip;             // the static block's +0x84 .. +0x8B (Gran Turismo 3's place)
    std::array<u8, 3> direct_speed{};  // the static block's +0xE4 (lateral), +0xE5, +0xE6 (slip ratio): km/h, 50 when 0
    rules::SlipForceGlobals globals;   // the camber force's reference and gain (0x504294 / 0x504298), the mean slip's switch
                                       // (0x506612), the steering mode (0x714880: 1 in a race; the slip angle takes each wheel's
                                       // offset, the drive block's +0x1C)
    std::array<std::array<u8, rules::kWheelLayoutSize>, 8> wheel_layouts{}; // 0x503FA8, by the parameter block's byte +0x30
    // The steering offset's settings (0x714880 ..) as the race's start builds them (0x3B57D0, Gran Turismo 4's 0x3F7BE0 rule):
    // the mode the static byte +0xB9, the scale +0xBA, each axle's limit +0xBB + k (tenths of a degree; the mode 0 when either
    // is 0), spring +0xBD + k (x 10) and damping +0xBF + k; the surfaces whose offset aims at 0 (0x504AC0 + surface). No
    // compliance steer.
    rules::SteerTables steer;
    // The pad's pedals (0x3DE270): with the controller settings' byte +0x11 (0x504961) set, an analog throttle (while
    // pressed) and brake move toward the pad's value by at most a step a frame (0x504AA8 / 0x504AAC the throttle's up and
    // down, 0x504AB0 / 0x504AB4 the brake's), except on a steering wheel (controller kinds 4 and 6).
    bool pedal_ramps = false;
    F throttle_up, throttle_down, brake_up, brake_down;
    Controls controls; // (its pedal steps are the four above)
    // The attitude's (0x3765F8): the static block's +0xD9 (the tires' pitch moment at the tire's radius; 0: at its width, as
    // Gran Turismo 3's) and +0xE0 (the heave's mode: 2 the wheels' loads along their grounds' normals onto the ground's up
    // axis, 1 the body's up axis onto it, 0 the body forces along the body's up axis; 0 means 1 for the in-line layout), the
    // in-line layout's roll terms (0x50429C .. 0x5042B0).
    u8 pitch_radius = 1, normal_mode = 0;
    std::array<F, 6> in_line;
    // The road's bumps by surface, as the race's start makes them (0x504478 .. 0x504508, from the static block): the
    // amplitude +0x1C + s x 0.001 (0x3A83126E), the wave number 2 pi (0x40C90FDA) / (+0x4C + s x 0.1 (0x3DCCCCCC)), the
    // speed ramp's switch +0x5C + s, the wave's mode +0x6C + s.
    ex::SurfaceBumps bumps{};
    // The rolling resistance's curve as the race's start makes it (0x504428 with 0x5043E8 / 0x504408): the static block's
    // +0x0B points, x +0x0C + k (x 2 km/h) x 1 / 3.6 (0x3E8E38E3), y +0x14 + k x 0.001 (0x3A83126E).
    u8 rolling_n = 0;
    std::array<F, 8> rolling_x, rolling_y;
    ex::BrakeHeatConsts brake_heat;
    // Four wheel drive system 3's fade and switch (0x504AD8: +2 the switch, +8 / +0x10 the fade's start and floor, % (0x3DFFD0
    // makes the floats between them)) and the stepless gearboxes' rates (0x504930: +0 / +1 kind 1's at full and no throttle,
    // +0xC / +0xD kind 2's down and up, x 0.01: 0x3DB730).
    ex::Awd3 awd3;
    ex::Stepless stepless;
    rules::HybridTables hybrid; // the hybrid's box's (0x504560, 0x5045B0, 0x504638, 0x504648)
    ConvertData convert; // what the converter reads (convert_prologue.h)
    u8 asm_keep = 0;     // 0x504B04: an entry with the stability control off keeps its switches' bits 6 and 7
};
// The engine's statics with Prologue's own values where they differ (the rolling resistance's curve; its per-surface
// coefficients, 0x504438, are the engine's).
void statics(ex::Statics& s, const Tables& t);
// The engine's race inputs with Prologue's own where they differ: the statics (above), the brake heat's constants (0x714870,
// made by 0x3B3740: 1 / (+0x7C x 9800 (0x46191FFF)), 1 / (+0x7D x 0.1 (0x3DCCCCCC) x 100), +0x7E / 3.6 (0x3E8E38E3), +0x7F
// x 0.01 of the static block, +0x7C and +0x7D 1 when 0), the road's bumps.
void race_inputs(ex::RaceInputs& r, const Tables& t);
Tables tables(const Program& program);

// A tire record's kind bytes (Prologue's parameter block: the record's +0x1B4 .. +0x1B6; its spec: the tire block's +0x6C ..
// +0x6E): the direct slip ratio, the direct lateral speed, the lateral force from curve A alone (rules::Tire's).
using TireKinds = std::array<std::array<u8, 3>, 6>;

// The wheel layout of the parameter block's row (P +0x30).
rules::WheelLayout wheel_layout(const Tables& t, u8 row);
// Gran Turismo 4's tire part of the record (rules::TireExtra) made from Prologue's parameter block: its 16 surfaces as
// surfaces 0 .. 15, the tire index, widths, camber curves and axle grips as they are; the direct lateral speed's scale pi / 2
// over the static byte +0xE4 (0x3BB138).
rules::TireExtra tire_extra(const ex::Params& p, const TireKinds& kinds, const Tables& t);

// 0x3BA450 (phase 8) with 0x3BA070: each wheel's slip ratio. Gran Turismo 4's (0x3513F8) but for the direct slip ratio:
// the difference of the hub's and the rim's speeds over a speed by the slip's state, the static byte +0xE6 driving (state 1),
// +0xE5 braking, each falling back to the other and then to 50 km/h (a division where Gran Turismo 4 multiplies by one
// scale).
void slip_ratios(ex::Car& c, const rules::TireExtra& x, const rules::WheelLayout& l, const Tables& t);

// A tire record's kind bytes from Prologue's parameter block (each record's +0x1B4 .. +0x1B6).
TireKinds tire_kinds(const Block& p);

// What a car of Prologue's step keeps beside the engine's record: Gran Turismo 4's tire part, the mass the forward step
// divides by (the parameter block's), its wheel layout.
struct CarPart {
    rules::TireExtra tires;     // (tires.steer_offset: each wheel's steering offset, the drive block's +0x1C)
    std::array<F, 2> tire_drop; // the parameter block's +0x28C + 4 a: taken off the tire's radius at the hub's rest point
    // Its part that changes with speed (the aerodynamic device, +0xE84 ..): the kind, the two speeds it changes at, how fast
    // it moves and the rear downforce it adds; no car of the disc has one.
    u8 part_kind = 0;
    F part_lo, part_hi, part_rate, part_lift;
    // The hybrid's box (gearbox kind 3; car +0x5DC): its state, the fuel used (+0x8) and the kilometers per liter (+0xC, by
    // 0x3CC298 each 30th step: F +0x4A0 the steps left, +0x3B0 / +0x3B4 the fuel and the odometer at the last one).
    rules::HybridState hybrid;
    F hybrid_fuel, hybrid_fuel_mark, hybrid_odo_mark;
    u16 mileage = 0;
    u8 mileage_steps = 30;
    u8 shift_class = 0;         // the driveline's +0x13 (0x3CBA70: 3 from 5,000 rpm, 2 from 4,500, else 1)
    u8 special_row = 0;         // its +0x33: the special table's row (the steering force's and the prefilter's kind 6 row)
    F front_share;              // the parameter block's +0x24: the front axle's share of the weight (the bump nudge's kind 3)
    std::array<F, 4> offset_rate; // the offset's rate, the drive block's +0x18
    std::array<F, 2> axle_load;   // the parameter block's +0xE6C + 4 k (the engine's +0xE54: each axle's static load)
    rules::ChassisExtra chassis;
    rules::WheelLayout layout;
};
// 0x3CC0B8: the hybrid's box at the car's set-up (nothing for another car).
void hybrid_start(const ex::Car& c, CarPart& x);
// 0x3CD4C0: the lowest gear whose up-shift speed less 500 rpm (2,000 in first) reaches a speed; 0x3C9108: a moving start at
// `v` m/s (the throttle open for a driver whose stretch is a straight: c.ai.mode 0).
int gear_for_speed(const ex::Car& c, F v);
void moving_start(ex::Car& c, F v);
// 0x3CC298: the kilometers per liter of a fuel mass (g) over a distance (m): 0 under a millimeter, at most 999.
u16 mileage(F fuel, F distance);
// The part from the car's record and Prologue's parameter block: the wheel layout of its +0x30, the tire records' kind bytes.
CarPart car_part(const ex::Car& c, const Block& p, const Tables& t);

// The parts of the parameter block (its image, Gran Turismo 3's layout or Prologue's: the same below the tire records) that
// the engine's record has no member for.
void image_part(CarPart& x, const u8* params);

// A car of Prologue's race as its set-up makes it (0x3C93A0): Prologue's spec (convert_prologue.h core_spec) of `s`, the
// spec of the equipment `eq` by Gran Turismo 4's builder, on a course of kind `course_kind` (2: dirt); the entry's aid
// switches (0x3C93A0: the stability control's switches off, its bits 6 and 7 kept while 0x504B04 is set; the traction
// control off); Prologue's converter at `base` (convert_prologue.h), its block read into c.p (the engine's layout) and the
// part beside it. `block`: the block too, when wanted.
CarPart build_car(db::SpecDb& db, const Spec& s, const Equipment& eq, int course_kind, bool asm_on, bool tcs_on, const Tables& t, u32 base,
                  ex::Car& c, Block* block = nullptr);

// Each wheel's tire's x1da for the forward step and the lateral forces: the record of the wheel's surface (Gran Turismo 4
// copies it in its peak force, 0x357418; Prologue reads the record).
void wheel_kinds(const ex::Car& c, CarPart& x);

// 0x3BB5D0: the forces across the car and the yaw. Gran Turismo 4's (0x352848) but for: no follower of the yaw
// acceleration, the lateral acceleration the force sum times the parameter block's inverse mass (+0x4C), and no computer
// driver's grip in the lateral acceleration over the tires' grip.
void lateral_yaw(ex::Car& c, const CarPart& x, bool airborne, const Tables& t);

// 0x3BBFE0 (phase 10): the engine's phase (brakes and clutch, rolling resistance, tire curves, the class grip, the
// longitudinal force, wheel torques, vectoring, axles, wheel speeds, engine speed and gearbox, the hold: each equal to
// Prologue's at every call) with Gran Turismo 4's tire slip force (0x3BB138; its steering mode 1, the wheels' offsets
// the drive block's +0x1C, the mean slip angle the record's F +0x420) and forward step (0x3BAC98), and its lateral forces.
void dynamics(ex::Car& c, CarPart& x, const ex::Statics& s, const ex::GearSettings& g, const ex::ClassGrip& grip, const Tables& t);

// 0x3B5A20 (phase 7): each wheel's steer angle as the engine's (equal at every call; it also leaves the angle's sine and
// cosine in the drive block for the engine's routines that read them, where Prologue keeps the offset's rate and the offset),
// then with the steering mode on each wheel's steering offset: Gran Turismo 4's (0x3F7EA8's offset, its spring, damping,
// scale and limits) but for a wheel without the slip's fade, whose offset aims at 0 (mode 1). No follower of the steering
// command, no compliance steer.
void wheel_steer(ex::Car& c, CarPart& x, const Tables& t);

// 0x3DDA10 (Gran Turismo 4's 0x3485D0): the steering follower, the command F +0x420 (the engine's steer_cmd) and its rate
// toward `target` for rate class r and mode m. The engine's (0x1E4368) but its gains and limits by class and mode, a pull
// toward the front wheels' mean slip direction `mean` while it lies beyond the command on the target's side (its own
// limit), and limits that move from the slow to the fast ones between two speeds.
void steer_follow(ex::Car& c, const SteerFollow& k, int r, int m, F target, F mean);

// 0x3DDC98 (the steering part of the pad's input, 0x3DE270): the command's bounds from the lock curves by speed, the slip
// direction's extension and the full lock, and the command: the follower for a digital pad and the sticks, the steering
// wheel's position, the controller kind 6's scaled position; the HUD's steering percentage. Unlike the engine's: the rate
// class is the steered wheels' least surface class (0x504AC0) for a pad and the course (dirt) for a steering wheel only;
// the mean slip direction is the steered wheels' (the wheel layout's wheels ahead of the center of gravity); no share of the
// extension (F +0x49C), no wheel positions (+0x16A4) kept.
void pad_steering(ex::Car& c, const ex::PadFrame& f, u8 course_kind, const rules::WheelLayout& l, const Controls& k);

// 0x3DEDE0's pad path (prefilter, then 0x3DED50 with 0x3DE270): the engine's (0x1E4D38's pad path: the HUD bytes, the
// throttle and brakes) with Prologue's steering (pad_steering; none for a special car of kind 4, P +0x32) and eased pedals
// (Tables::pedal_ramps).
// The input phase's frame (0x3DEDE0): the steering wheel's position kept (+0x16A4, the last one +0x16A6) and the steering
// prefiltered by its change (Controls::prefilter_*), within +-4096.
ex::PadFrame prefilter(ex::Car& c, const ex::PadFrame& f, const Controls& k, u8 special_row = 0);
// 0x3DE270 past its steering: the throttle, brake and third axis, the reverse, shift and clutch requests (0x3DD608; the hybrid's
// 0x3DD798 and 0x3DD858), a stepless gearbox's creep. Unlike the engine's: digital ramps of its own, an analog pedal eased by
// them (Tables::pedal_ramps; not a steering wheel's, kinds 4 and 6), the wheels' curves for both kinds (the throttle's not for
// the hybrid), a special car of kind 8 (P +0x32) without a clutch.
void pad_pedals(ex::Car& c, const ex::PadFrame& f, const Tables& t);
void driver_input_pad(ex::Car& c, const ex::PadConstants& k, const ex::PadFrame& f, u8 course_kind, const rules::WheelLayout& l,
                      const Tables& t, u8 special_row = 0); // (`special_row`: P +0x33, a kind 6 wheel's prefilter row)

// 0x376440: the tires' longitudinal forces' pitch moment and lift (Gran Turismo 4's 0x36AF30 on the wheel layout, the
// force's lever the tire's radius or, with the static block's +0xD9 0, its width).
void pitch_terms(const ex::Car& c, const rules::WheelLayout& l, const Tables& t, F& moment, F& lift);
// 0x3765F8 (phase 19): the start's hold (0x376330: the engine's, with Gran Turismo 4's axes), else the pitch and roll from the
// body forces and dampers by the wheel layout (the in-line layout's lean), Gran Turismo 4's axes (0x377310) and the heave:
// Gran Turismo 4's (0x36B0C8) but every damper counted, the sums times the inverse sprung mass, each wheel's whole load
// (no preload taken off) projected onto the ground's up axis and no speed part's lift. `in_line_lean` is the record's
// F +0x3AC (the in-line layout's lean term, ❓ not kept: 0).
void attitude(ex::Car& c, const rules::WheelLayout& l, const Tables& t, F in_line_lean = F());

// 0x375A38 (Gran Turismo 4's 0x36A080): each wheel's place along its travel: the hub's rest point below it (0x3753C8:
// -x_static - (radius - tire_drop) + offset) along the body's up axis (0x375458), its height over the ground (the engine's
// ground0 and bump) times the ground's up z over the terrain factor (one for every wheel, where Gran Turismo 4 takes each
// wheel's normal).
void susp_coords(ex::Car& c, const CarPart& x);
// 0x3754D8's query points (0x375458 per axis, Gran Turismo 4's 0x3697A8): each wheel's ground record's point the hub's
// moved along the body's up axis by the hub's rest drop, in the course's frame (x, height, -y).
void query_points(ex::Car& c, const CarPart& x);
// 0x3754D8's bumps on the ground the query found: each wheel on the road gets its surface's wave at the query point times
// the amplitude (times the wheel's speed ramp where the surface says so), off the road 0 (the engine's wave, 0x375340).
void road_bumps(ex::Car& c, const CarPart& x, const ex::SurfaceBumps& b);
// 0x3754D8: the query points, the course's query of all four (Gran Turismo 3's 0x18FB58 = Prologue's 0x3882D0, the engine's
// CourseGround), then for each wheel on the road its ground height, surface index (0x3B46C8 = the engine's surface_index),
// triangle bytes and bump; the road segment hint; F +0x535 bit 4 with a wheel on a pit-lane triangle and `mark_flagged`.
ex::GroundCounts ground_contact(ex::Car& c, const CarPart& x, const ex::CourseGround& course, const ex::SurfaceBumps& b, bool mark_flagged);
// 0x375D00: the terrain factor (Gran Turismo 4's: the ground's up axis . the body's), the places (susp_coords) and the mask
// of the wheels past their bump stop (0x375B88, by the wheel layout).
void terrain(ex::Car& c, const CarPart& x);
// 0x376EC8 (Gran Turismo 4's 0x36BA88 on Prologue's places): a wheel past its bump stop lifts and turns the body back over
// it, by the wheel layout; Gran Turismo 4's axes; the downward speed held to the ground plane's.
void bump_nudge(ex::Car& c, const CarPart& x);

// 0x3C4D80: the ground's pitch, roll and height as the engine's (Gran Turismo 3's) and its axes as Gran Turismo 4's
// (0x377310: the pitch before the roll).
void ground_frame(ex::Car& c);

} // namespace gt4r::sim::prologue
