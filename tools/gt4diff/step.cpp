// gt4r_diff's checks of the race step (diff.h): the program's own step run in the interpreter on a race
// its own initialisation set up (orig.h), with our routines compared in place at every call (taps.h).

#include "diff.h"
#include "collide_check.h"
#include "orig.h"
#include "taps.h"

#include "gt4r/sim/exact_bridge.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_body.h"
#include "gt4r/sim/ground.h"
#include "gt4r/sim/rules_chassis.h"
#include "gt4r/sim/rules_drive.h"
#include "gt4r/sim/rules_phases.h"
#include "gt4r/sim/rules_start.h"
#include "gt4r/sim/rules_effects.h"
#include "gt4r/sim/rules_feel.h"
#include "gt4r/sim/rules_step.h"
#include "gt4r/sim/rules_tires.h"
#include "gt4r/sim/rules_wear.h"
#include "gt4r/sim/rules_speeds.h"
#include "gt4r/sim/rules_wheels.h"
#include "sim/exact/tick.h"

#include <cmath>
#include <optional>
#include <cstdio>
#include <cstdlib>

namespace gt4r::diff {

namespace {

// A routine's car, where it is its first argument: the record, or its state block.
[[maybe_unused]] u32 car_a0(Ee& e) { return u32(e.a(0)); }
const sim::Program* g_program = nullptr; // (the run's program, for the taps that read its tables)
[[maybe_unused]] u32 car_of_state(Ee& e) { return u32(e.a(0)) - kF; }

// The race's wear rules a car's routines read (its conductor + 4).
sim::rules::WearRules wear_rules_of(TapCall& t) {
    u8 b[0x6C];
    t.ee.read_bytes(t.ee.r32(t.race.car(t.car_index) + 4) + 4, b, sizeof b);
    return sim::rules::wear_rules(b);
}

// The race's per-surface tables as the program's race start left them.
sim::rules::RoadTables road_tables(Ee& ee) {
    sim::rules::RoadTables r;
    r.rolling_curve.n = s16(ee.r16(sim::rules::kRollingCurve));
    const u32 xs = ee.r32(sim::rules::kRollingCurve + 4), ys = ee.r32(sim::rules::kRollingCurve + 8);
    for (u32 k = 0; k < 8; ++k) r.rolling_curve.x[k] = gt3r::ee::F::raw(ee.r32(xs + 4 * k)), r.rolling_curve.y[k] = gt3r::ee::F::raw(ee.r32(ys + 4 * k));
    for (u32 s = 0; s < 24; ++s) r.rolling_mu[s] = gt3r::ee::F::raw(ee.r32(sim::rules::kRollingMu + 4 * s));
    return r;
}

sim::rules::SlipForceGlobals slip_globals(Ee& ee) {
    using namespace sim::rules;
    SlipForceGlobals g;
    g.steer_mode = ee.r32(kSteerMode), g.camber_ref = gt3r::ee::F::raw(ee.r32(kCamberRef));
    g.camber_gain = gt3r::ee::F::raw(ee.r32(kCamberGain)), g.mean_on = ee.r8(kMeanSlipOn);
    return g;
}

// The driveline's tables as the program's race set-up left them.
sim::rules::DriveTables drive_tables(Ee& ee) {
    u8 a[14], st[14], g[12];
    ee.read_bytes(sim::rules::kAwdFade, a, 14), ee.read_bytes(sim::rules::kSteplessBytes, st, 14), ee.read_bytes(sim::rules::kControllerSettings, g, 12);
    sim::rules::DriveTables r = sim::rules::drive_tables(a, st, g);
    if (g_program) r.hybrid = sim::rules::hybrid_tables(*g_program);
    u8 m[0x1B];
    ee.read_bytes(sim::rules::kMotorBytes, m, sizeof m);
    r.motors = sim::rules::motor_tables(m);
    return r;
}

// A computer driver's grip by tire class (0x467470: control source 1 / 2 or a driver kind; its parameter block +0xA8).
std::optional<std::array<gt3r::ee::F, 3>> driver_grip_of(TapCall& t) {
    const u32 car = t.race.car(t.car_index), F = car + kF;
    const gt3r::s8 control = gt3r::s8(t.ee.r8(F + 0x466));
    if (control != 1 && control != 2 && t.ee.r8(F + 0x467) == 0) return std::nullopt;
    const u32 d = car + 0x14, v0 = t.ee.r32(d + 0xE8), a1 = t.ee.r32(d + 0xE4);
    const u32 pb = t.ee.r32(a1 + 4 * u32(t.ee.r8(v0 + 0x544)) + 0x10024);
    std::array<gt3r::ee::F, 3> g;
    for (u32 k = 0; k < 3; ++k) g[k] = gt3r::ee::F::raw(t.ee.r32(pb + 0xA8 + 4 * k));
    return g;
}

sim::rules::BodyGlobals body_globals(Ee& ee) {
    return sim::rules::body_globals([&](u32 a) { return ee.r32(a); }, [&](u32 a) { return ee.r8(a); });
}

void add_taps(Taps& taps) {
    using namespace sim::rules;
    taps.add({0x369888, "road bumps", car_a0,
              [](TapCall& t) {
                  std::array<u8, 4> surface{}, material{};
                  for (size_t w = 0; w < 4; ++w)
                      material[w] = t.car.g[w].material, surface[w] = u8(sim::surface_index(t.car.g[w].material, t.car.g[w].flags));
                  const BumpTables b = bump_tables([&](u32 a) { return t.ee.r32(a); }, [&](u32 a) { return t.ee.r8(a); });
                  road_bumps(t.car, t.rec.layout, b, surface, material);
                  course_out(t.car, t.rec.x, t.rec.layout, b.soft);
              },
              {"w.ground", "w.surface", "w.x25", "w.surface_flags", "road_seg", "flags", "x535", "aids."}});
    taps.add({0x3F3090, "aero", car_a0, [](TapCall& t) {
                  aero(t.car, t.rec.x.chassis, t.rec.x.body.speed_part_kind, t.rec.x.body.speed_part_lift,
                       {gt3r::ee::F::raw(t.ee.r32(kAeroGlobals - 4)), gt3r::ee::F::raw(t.ee.r32(kAeroGlobals)), gt3r::ee::F::raw(t.ee.r32(kAeroGlobals + 4)),
                        gt3r::ee::F::raw(t.ee.r32(kAeroGlobals + 8)), gt3r::ee::F::raw(t.ee.r32(kAeroGlobals + 12))});
              }});
    taps.add({0x3458D0, "hub offsets xy", car_a0, [](TapCall& t) { sim::rules::hub_offsets_xy(t.car, t.rec.layout); }});
    // (0x35DE20 itself: the interpreter never stops at its entry; its parts are tapped, and gt4r_diff ai's whole step
    // compares all of it)
    taps.add({0x35A678, "flaps", car_a0, [](TapCall& t) {
                  u32 state = t.ee.r32(sim::rules::kRandomState);
                  sim::rules::flaps(t.car, t.rec.layout, state);
                  t.mem_i("random state", sim::rules::kRandomState, state);
              }});
    taps.add({0x367148, "sliding measures", car_a0, [](TapCall& t) {
                  u8 b[0x210];
                  t.ee.read_bytes(sim::rules::kSlideBytes, b, sizeof b);
                  const sim::rules::SlideTables k = sim::rules::slide_tables(b);
                  const u8 kind = t.ee.r8(t.ee.r32(t.race.car(t.car_index) + 4) + 0x10000 - 0x3423);
                  sim::rules::slide_effects(t.car, t.rec.layout, k, kind);
              }});
    taps.add({0x352EA0, "tire wear", car_a0, [](TapCall& t) {
                  sim::rules::tire_wear(t.car, t.rec.x.wear, t.rec.x.tires, t.rec.layout, wear_rules_of(t));
              }});
    // The views' and the pad's blocks in the conductor (cond + 0xCAB8 / + 0xCA58, by the car's index F + 0x440).
    struct ViewAt {
        static u32 block(TapCall& t, u32 base, u32 size) {
            const u32 car = t.race.car(t.car_index);
            return t.ee.r32(car + 4) + base + size * t.ee.r8(car + kF + 0x440);
        }
        static sim::rules::ViewState read(TapCall& t) {
            const u32 b = block(t, sim::rules::kViewBlock, sim::rules::kViewSize);
            auto f = [&](u32 o) { return gt3r::ee::F::raw(t.ee.r32(b + o)); };
            sim::rules::ViewState v;
            v.heading = f(0), v.heading_rate = f(4), v.heading_acc = f(8), v.pitch = f(0xC), v.pitch_rate = f(0x10);
            v.sway_pitch_rate = f(0x14), v.sway_pitch = f(0x18), v.sway_roll_rate = f(0x28), v.sway_roll = f(0x2C);
            return v;
        }
        static sim::rules::FeelTables tables(TapCall& t) {
            sim::rules::FeelTables k;
            k.view_heading_gain = gt3r::ee::F::raw(t.ee.r32(0x69F4C8)), k.view_heading_damp = gt3r::ee::F::raw(t.ee.r32(0x69F4D0));
            k.sway_pitch_on = t.ee.r8(sim::rules::kSwayBytes), k.sway_roll_on = t.ee.r8(sim::rules::kSwayBytes + 5);
            for (u32 j = 0; j < 4; ++j)
                k.sway_pitch[j] = gt3r::ee::F::raw(t.ee.r32(0x6202C8 + 4 * j)), k.sway_roll[j] = gt3r::ee::F::raw(t.ee.r32(0x6202D8 + 4 * j));
            return k;
        }
    };
    taps.add({0x345658, "view follower", car_a0, [](TapCall& t) {
                  sim::rules::ViewState v = ViewAt::read(t);
                  sim::rules::view_follow(t.car, v, ViewAt::tables(t));
                  const u32 b = ViewAt::block(t, sim::rules::kViewBlock, sim::rules::kViewSize);
                  t.mem_f("view heading", b, v.heading), t.mem_f("view heading rate", b + 4, v.heading_rate), t.mem_f("view heading acc", b + 8, v.heading_acc);
                  t.mem_f("view pitch", b + 0xC, v.pitch), t.mem_f("view pitch rate", b + 0x10, v.pitch_rate);
              }});
    taps.add({0x34A668, "sway pitch", car_a0, [](TapCall& t) {
                  sim::rules::ViewState v = ViewAt::read(t);
                  sim::rules::FeelTables k = ViewAt::tables(t);
                  k.sway_roll_on = 0;
                  sim::rules::sway(t.car, v, k, t.rec.x.feel.blend);
                  const u32 b = ViewAt::block(t, sim::rules::kViewBlock, sim::rules::kViewSize);
                  t.mem_f("sway pitch rate", b + 0x14, v.sway_pitch_rate), t.mem_f("sway pitch", b + 0x18, v.sway_pitch);
              }});
    taps.add({0x34A788, "sway roll", car_a0, [](TapCall& t) {
                  sim::rules::ViewState v = ViewAt::read(t);
                  sim::rules::FeelTables k = ViewAt::tables(t);
                  k.sway_pitch_on = 0;
                  sim::rules::sway(t.car, v, k, t.rec.x.feel.blend);
                  const u32 b = ViewAt::block(t, sim::rules::kViewBlock, sim::rules::kViewSize);
                  t.mem_f("sway roll rate", b + 0x28, v.sway_roll_rate), t.mem_f("sway roll", b + 0x2C, v.sway_roll);
              }});
    taps.add({0x36CEE8, "pad rumble", car_a0, [](TapCall& t) {
                  const u32 b = ViewAt::block(t, sim::rules::kPadBlock, sim::rules::kPadSize);
                  std::array<u8, 2> pad{t.ee.r8(b), t.ee.r8(b + 1)};
                  sim::rules::pad_rumble(t.car, t.rec.layout, pad);
                  t.mem_i("pad +0", b, pad[0], 1), t.mem_i("pad level", b + 1, pad[1], 1);
              }});
    taps.add({0x350080, "engine sound", car_a0, [](TapCall& t) {
                  const u32 car = t.race.car(t.car_index), info = t.ee.r32(car);
                  const u32 e0 = t.ee.r32(info + 0xE0), ghost = e0 ? 0 : t.ee.r32(t.ee.r32(info + 0x84) + 0x10000 - 0xFB0);
                  sim::rules::FeelTables k;
                  for (u32 j = 0; j < 32; ++j) k.cue[j] = gt3r::ee::F::raw(t.ee.r32(sim::rules::kCueTable + 4 * j));
                  sim::rules::engine_sound(t.car, k, t.rec.x.drive.d.kind, sim::rules::draw_mode(t.car, t.ee.r8(car + kF + 0x4B2), e0, ghost));
              }});
    taps.add({0x366718, "effects reset", car_a0, [](TapCall& t) {
                  const u32 car = t.race.car(t.car_index), info = t.ee.r32(car);
                  const u32 e0 = t.ee.r32(info + 0xE0), ghost = e0 ? 0 : t.ee.r32(t.ee.r32(info + 0x84) + 0x10000 - 0xFB0);
                  sim::rules::effects_reset(t.car, t.rec.layout, sim::rules::draw_mode(t.car, t.ee.r8(car + kF + 0x4B2), e0, ghost));
              }});
    taps.add({0x354AC0, "masses", car_a0, [](TapCall& t) { sim::rules::masses(t.car, t.rec.x.drive.fuel, t.rec.x.chassis.mass, t.rec.x.body.sprung_mass); }});
    taps.add({0x35D920, "hub velocities", car_a0, [](TapCall& t) {
                  sim::rules::hub_velocities(t.car, t.rec.layout, sim::rules::speed_tables(*g_program, t.rec.x.steer.lean_row), t.rec.x.body.in_line_x480);
              }});
    taps.add({0x35D990, "free wheels", car_a0, [](TapCall& t) { sim::rules::free_wheels(t.car, t.rec.layout); }});
    taps.add({0x345A50, "speed ramp", car_a0, [](TapCall& t) { ex::speed_ramp(t.car); }});
    taps.add({0x345A20, "hub offsets", car_a0, [](TapCall& t) { sim::rules::hub_offsets(t.car, t.rec.layout); }});
    taps.add({0x344040, "wheel spin", car_a0, [](TapCall& t) {
                  const sim::rules::SpeedTables k = sim::rules::speed_tables(*g_program, t.rec.x.steer.lean_row);
                  sim::rules::wheel_spin(t.car, t.rec.x.feel.anim, t.rec.layout, k.special_x10, t.rec.x.wear.category, t.rec.x.body.in_line_x65c,
                                         k.throttle_share, k.brake_share);
              }});
    taps.add({0x35D350, "ground frame", car_a0, [](TapCall& t) { ground_frame(t.car, t.rec.layout); }});
    taps.add({0x3697A8, "query points", car_a0, [](TapCall& t) { query_points(t.car, t.rec.x.body, t.rec.layout); }});
    taps.add({0x36A490, "terrain", car_a0, [](TapCall& t) { terrain(t.car, t.rec.x.body, t.rec.layout); }});
    taps.add({0x36A758, "suspension", car_a0, [](TapCall& t) { suspension(t.car, t.rec.x.body, t.rec.x.chassis, t.rec.layout, body_globals(t.ee)); }});
    // (first the start hold while the hold counter runs, 0x36AE20: the line's heading 0 without lines)
    taps.add({0x36B0C8, "attitude", car_a0, [](TapCall& t) {
                  if (!attitude_hold(t.car, t.rec.x.body, gt3r::ee::F()))
                      attitude(t.car, t.rec.x.body, t.rec.x.chassis, t.rec.x.tires, t.rec.layout, body_globals(t.ee));
              }});
    taps.add({0x36BA88, "bump nudge", car_a0, [](TapCall& t) { bump_nudge(t.car, t.rec.x.body, t.rec.layout); }});
    taps.add({0x3511A0, "brakes and clutch", car_a0, [](TapCall& t) { brakes_and_clutch(t.car, t.rec.x.drive, t.rec.layout); }});
    taps.add({0x36D230, "wheel torques", car_a0, [](TapCall& t) { wheel_torques(t.car, t.rec.x.drive, t.rec.layout); }});
    // Layout 7's motors' routines (only the Acura DN-X calls them).
    taps.add({0x3561D0, "motors: demand", car_a0, [](TapCall& t) {
                  t.ret_f("returns", sim::rules::motor_demand(t.car, t.rec.x.drive.hybrid, drive_tables(t.ee).motors));
              }});
    taps.add({0x356168, "motors: level", car_a0, [](TapCall& t) { t.ret_f("returns", sim::rules::motor_level(t.rec.x.drive.hybrid, drive_tables(t.ee).motors)); }});
    taps.add({0x3560F0, "motors: engine throttle", car_a0, [](TapCall& t) {
                  t.ret_f("returns", sim::rules::motor_engine_throttle(t.car, drive_tables(t.ee).motors));
              }});
    taps.add({0x356300, "motors: wanted lateral", car_a0, [](TapCall& t) {
                  t.ret_f("returns", sim::rules::motor_wanted_lateral(t.car, t.rec.x.tires, t.rec.layout));
              }});
    taps.add({0x356418, "motors: front", car_a0, [](TapCall& t) {
                  front_motors(t.car, t.rec.x.drive, t.rec.x.tires, t.rec.layout, drive_tables(t.ee).motors);
              }});
    taps.add({0x356BB8, "motors: rear", car_a0, [](TapCall& t) {
                  t.ret_f("returns", sim::rules::rear_motor(t.car, t.rec.x.drive.hybrid, drive_tables(t.ee).motors));
              }});
    taps.add({0x355F80, "motors: idle charge", car_a0, [](TapCall& t) {
                  t.ret_f("returns", sim::rules::idle_charge(t.car, t.rec.x.drive, drive_tables(t.ee).motors));
              }});
    taps.add({0x36D2F8, "vectoring", car_a0, [](TapCall& t) {
                  if (t.rec.x.drive.d.layout == 7) front_motors(t.car, t.rec.x.drive, t.rec.x.tires, t.rec.layout, drive_tables(t.ee).motors);
                  vectoring(t.car, t.rec.x.drive, t.rec.layout);
              }});
    taps.add({0x36D818, "axle step", car_a0, [](TapCall& t) { axle_step(t.car, t.rec.x.drive, t.rec.layout, drive_tables(t.ee)); }});
    taps.add({0x36E4B8, "wheel speeds", car_a0, [](TapCall& t) { wheel_speeds(t.car, t.rec.x.drive, t.rec.layout); }});
    taps.add({0x3519B8, "engine speed", car_a0, [](TapCall& t) {
                  const sim::rules::DriveTables k = drive_tables(t.ee);
                  engine_speed(t.car, t.rec.x.drive, t.rec.layout, &k);
              }});
    taps.add({0x351DC8, "gear", car_a0, [](TapCall& t) { gear_step(t.car, t.rec.x.drive, t.rec.layout, t.rec.x.tires, drive_tables(t.ee)); }});
    taps.add({0x3F38D8, "brake heat", car_a0, [](TapCall& t) {
                  ex::BrakeHeatConsts k;
                  k.c0 = gt3r::ee::F::raw(t.ee.r32(kBrakeHeatConsts)), k.c1 = gt3r::ee::F::raw(t.ee.r32(kBrakeHeatConsts + 4));
                  k.c2 = gt3r::ee::F::raw(t.ee.r32(kBrakeHeatConsts + 8)), k.c3 = gt3r::ee::F::raw(t.ee.r32(kBrakeHeatConsts + 12));
                  brake_heat(t.car, t.rec.layout, k, t.rec.x.drive.d.tire_radius);
              }});
    taps.add({0x3554A0, "engine torque", car_a0, [](TapCall& t) { t.ret_f("returns", engine_torque(t.car, t.rec.x.drive, gt3r::ee::F::raw(t.ee.fpr[12]))); }});
    taps.add({0x3658A0, "step length", car_a0, [](TapCall& t) { ex::step_length(t.car); }});
    taps.add({0x356EF0, "wear grip", car_a0, [](TapCall& t) {
                  t.ret_i("returns", wear_grip(t.car, t.rec.x.wear, t.rec.x.tires, t.rec.layout, wear_rules_of(t)));
              }});
    taps.add({0x357418, "peak force", car_a0, [](TapCall& t) {
                  const bool wear = wear_grip(t.car, t.rec.x.wear, t.rec.x.tires, t.rec.layout, wear_rules_of(t));
                  const auto g = driver_grip_of(t);
                  peak_force(t.car, t.rec.x.tires, t.rec.layout, wear, g ? &*g : nullptr);
              }});
    taps.add({0x351778, "slip ratios", car_a0, [](TapCall& t) {
                  u8 b[8];
                  t.ee.read_bytes(kSlipBytes, b, 8);
                  slip_ratios(t.car, t.rec.x.tires, t.rec.layout, slip_bytes(b));
              }});
    taps.add({0x351850, "tire curves", car_a0, [](TapCall& t) { tire_curves(t.car, t.rec.x.tires, t.rec.layout); }});
    taps.add({0x3518E8, "tire long force", car_a0, [](TapCall& t) {
                  u8 b[8];
                  t.ee.read_bytes(kSlipBytes, b, 8);
                  tire_long_force(t.car, t.rec.layout, slip_bytes(b));
              }});
    taps.add({0x352408, "tire slip force", car_a0, [](TapCall& t) { tire_slip_force(t.car, t.rec.x.tires, t.rec.layout, slip_globals(t.ee)); }});
    taps.add({0x352848, "lateral and yaw", car_a0, [](TapCall& t) {
                  const auto g = driver_grip_of(t);
                  lateral_yaw(t.car, t.rec.x.chassis, t.rec.x.tires, t.rec.layout, t.ee.a(1) != 0, slip_globals(t.ee), g ? &*g : nullptr);
              }});
    taps.add({0x352C90, "stop hold", car_a0, [](TapCall& t) { stop_hold(t.car, t.rec.layout, t.ee.a(1) != 0); }});
    taps.add({0x351FA8, "forward step", car_a0, [](TapCall& t) { forward_step(t.car, t.rec.x.chassis, t.rec.x.tires, t.rec.layout); }});
    taps.add({0x3534A0, "rolling resistance", car_a0, [](TapCall& t) { rolling_resistance(t.car, t.rec.layout, road_tables(t.ee)); }});
    taps.add({0x357690, "slip angles", car_a0, [](TapCall& t) { slip_angles(t.car, t.rec.layout); }});
    taps.add({0x343ED0, "save previous", car_a0, [](TapCall& t) { ex::save_previous(t.car); }});
    // The engine's routines where Gran Turismo 4's are not ported yet (to see how they differ).
    if (std::getenv("GT4R_STEP_ENGINE_TAPS")) {
        taps.add({0x3511A0, "engine: brakes and clutch", car_a0, [](TapCall& t) { ex::brakes_and_clutch(t.car); }});
        taps.add({0x36D230, "engine: wheel torques", car_a0, [](TapCall& t) { ex::wheel_net_torque(t.car); }});
        taps.add({0x36D2F8, "engine: vectoring", car_a0, [](TapCall& t) { ex::torque_vectoring(t.car, t.inputs.statics.hybrid); }});
        taps.add({0x36D818, "engine: axle step", car_a0, [](TapCall& t) {
                      ex::axle_step(t.car, t.inputs.statics.awd3, t.inputs.statics.stepless, t.inputs.statics.hybrid);
                  }});
        taps.add({0x36E4B8, "engine: wheel speeds", car_a0, [](TapCall& t) { ex::wheel_speeds(t.car); }});
        taps.add({0x3519B8, "engine: engine speed", car_a0, [](TapCall& t) { ex::engine_speed(t.car, t.inputs.statics.hybrid); }});
        taps.add({0x351DC8, "engine: gear", car_a0, [](TapCall& t) { ex::gear_step(t.car, t.inputs.gears); }});
        taps.add({0x3F38D8, "engine: brake heat", car_a0, [](TapCall& t) { ex::brake_heat(t.car, t.inputs.brake_heat); }});
        taps.add({0x3F7EA8, "engine: wheel steer", car_a0, [](TapCall& t) { ex::wheel_steer(t.car); }});
        taps.add({0x36A490, "engine: terrain", car_a0, [](TapCall& t) { ex::terrain_factor(t.car); }});
        taps.add({0x36A758, "engine: suspension", car_a0, [](TapCall& t) { ex::suspension(t.car, nullptr); }});
        taps.add({0x36B0C8, "engine: attitude", car_a0, [](TapCall& t) { ex::attitude(t.car, gt3r::ee::F()); }});
        taps.add({0x36BA88, "engine: bump nudge", car_a0, [](TapCall& t) { ex::bump_nudge(t.car); }});
        taps.add({0x3F3090, "engine: aero", car_a0, [](TapCall& t) { ex::aero(t.car, t.inputs.prep.slip); }});
        taps.add({0x35D350, "engine: ground frame", car_a0, [](TapCall& t) { ex::ground_frame(t.car); }});
        taps.add({0x35DE20, "engine: speeds", car_a0, [](TapCall& t) { ex::speed_bookkeeping(t.car, t.inputs.prep.condition); }});
        taps.add({0x344040, "engine: wheel spin", car_a0, [](TapCall& t) { ex::wheel_spin(t.car); }});
        taps.add({0x3458D0, "engine: hub offsets xy", car_a0, [](TapCall& t) { ex::hub_offsets_xy(t.car); }});
        taps.add({0x345A50, "engine: speed ramp", car_a0, [](TapCall& t) { ex::speed_ramp(t.car); }});
        taps.add({0x3457B0, "engine: hub heights", car_a0, [](TapCall& t) { ex::hub_offsets_z(t.car); }});
    }
    taps.add({0x36C9F8, "body axes", [](Ee& e) { return u32(e.a(0)) - 0x110; }, [](TapCall& t) { sim::rules::body_axes(t.car); }});
}

// The pad of a test drive at step i: a standing start, through the gears, a lift, then turns each way and the brakes.
Pad drive(int i) {
    Pad p;
    const int t = i - 150;
    if (t < 0) return p;
    p.throttle = 1.0f;
    // GT4R_STEP_LONG: a long run instead: full throttle with lifts every 20 s, light steering, braking from time to time (to
    // run down and charge a hybrid's store, wear tires, burn fuel).
    if (std::getenv("GT4R_STEP_LONG")) {
        const int s = t % 1200;
        if (s > 1000 && s < 1100) p.throttle = 0.0f, p.brake = s < 1060 ? 0.9f : 0.3f;
        p.steer = (t / 300) % 2 ? 0.05f : -0.05f;
        return p;
    }
    if (t > 500 && t < 560) p.throttle = 0.0f;
    if (t > 600 && t < 700) p.steer = 0.35f;
    if (t > 760 && t < 860) p.steer = -0.5f;
    if (t > 900) p.throttle = 0.0f, p.brake = 0.8f;
    return p;
}

} // namespace

void add_step_taps(Taps& taps) {
    add_taps(taps);
    add_taps_near(taps);
    add_taps_phases(taps);
}
void set_step_program(const sim::Program* program) { g_program = program; }

StepResult run_step(Disc& disc, bool print, const std::string& course_label, const std::string& car_label, int step_count) {
    StepResult result;
    db::SpecDb sdb = open_db(disc);
    const sim::Program program = sim::Program::load(disc);
    g_program = &program;
    if (!program.known()) {
        result.error = "the disc's program is not a mapped build";
        if (print) std::printf("step: %s\n", result.error.c_str());
        return result;
    }
    Ee ee;
    boot(ee, disc, sdb);
    OrigRace o;
    std::string err;
    const char* course = std::getenv("GT4R_STEP_COURSE");
    const char* car = std::getenv("GT4R_STEP_CAR");
    const std::string course_name = !course_label.empty() ? course_label : course ? course : "tsukuba";
    const std::string car_name = !car_label.empty() ? car_label : car ? car : "nsx_r_02";
    const ex::ProgramData data = sim::core_program(program);
    const ex::RaceConfig config{};
    const ex::RaceInputs inputs = ex::race_inputs(data, ex::Conductor::SinglePlayer, config, false, 0);
    if (!setup_race(ee, disc, sdb, course_name, {car_name}, 1, o, err, false, nullptr, [&] { install_setup_taps(ee, o, inputs); })) {
        result.error = err;
        if (print) std::printf("step: %s\n", err.c_str());
        return result;
    }
    Taps taps(ee, o, inputs);
    add_taps(taps);
    add_taps_aids(taps);
    // GT4R_TAP_WRITES="352EA0,353218": routines of a car (a0) tapped with nothing of ours run, to list what they write.
    if (const char* list = std::getenv("GT4R_TAP_WRITES")) {
        static std::vector<std::string> names;
        for (std::string rest = list; !rest.empty();) {
            const size_t comma = rest.find(',');
            const std::string a = rest.substr(0, comma);
            rest = comma == std::string::npos ? "" : rest.substr(comma + 1);
            names.push_back("writes of " + a);
            taps.add({u32(std::stoul(a, nullptr, 16)), names.back().c_str(), car_a0, [](TapCall&) {}});
        }
    }
    add_taps_steer(taps);
    add_taps_near(taps);
    add_taps_phases(taps);
    add_taps_input(taps);
    add_taps_start(taps);
    // GT4R_STEP_COLLIDE: the collision step and the road query against ours at every call (collide_check.cpp).
    std::unique_ptr<CollideCheck> collide_check;
    if (std::getenv("GT4R_STEP_COLLIDE")) {
        collide_check = std::make_unique<CollideCheck>(ee, o, disc, course_name);
        collide_check->install(true, true);
    }
    if (std::getenv("GT4R_DUMP")) {
        const u32 at = o.car(0);
        for (u32 off = 0; off < kCarSize; off += 4) {
            const u32 v = ee.r32(at + off);
            if (!v) continue;
            const float f = ee.rf(at + off);
            if (std::fabs(f) > 1e-6f && std::fabs(f) < 1e7f) std::printf("  +%04X %12.6f\n", off, double(f));
            else std::printf("  +%04X %08X\n", off, v);
        }
    }
    // The race's wear switches (the conductor + 4 + 0x68 / 0x69), to run the wear's branches: bit 0 the tread wear, bit 1 the second.
    { // The wear rules as the configuration made them (conductor + 4) against ours from the same rule block.
        u8 rb[0x80], prog[0x6C];
        ee.read_bytes(o.cond + 0xF818, rb, sizeof rb);
        ee.read_bytes(o.cond + 4, prog, sizeof prog);
        const bool second = ee.r8(o.cond + 0x10000 - 0x3420) == 1 && ee.r8(o.cond + 0x10000 - 0x3423) == 0;
        const auto ours = sim::rules::wear_rules_block(rb, second, program);
        size_t off = 0;
        for (u32 i = 0; i < 0x6C; ++i) off += prog[i] != ours[i];
        if (print) std::printf("  wear rules (second wear %s, tread %s): %s\n", second ? "on" : "off", prog[0x68] ? "on" : "off",
                               off ? "differ" : "equal to the program's");
        if (off && print)
            for (u32 i = 0; i < 0x6C; i += 4) {
                u32 a, b;
                std::memcpy(&a, prog + i, 4), std::memcpy(&b, &ours[i], 4);
                if (a != b) std::printf("    +%02X the program's %08X, ours %08X\n", i, a, b);
            }
    }
    if (const char* w = std::getenv("GT4R_STEP_WEAR")) {
        const int m = std::atoi(w);
        ee.w8(o.cond + 4 + 0x68, u8(m & 1)), ee.w8(o.cond + 4 + 0x69, u8((m >> 1) & 1));
        // A made-up curve for each (the race's own are 0 here), and the wheels' wears spread over every branch.
        const float tread[8] = {-0.5f, 0.1f, 0.2f, 0.05f, 0.5f, 0.15f, 0.8f, 0.3f}, second[6] = {-0.4f, 0.1f, 0.3f, 0.1f, 0.9f, 0.25f};
        for (u32 k = 0; k < 8; ++k) ee.wf(o.cond + 4 + 4 * k, tread[k]);
        for (u32 k = 0; k < 6; ++k) ee.wf(o.cond + 4 + 0x50 + 4 * k, second[k]);
        // the tread wear's pit grip, cap and slip extra; the second wear's rate, weights, fall and force scale (the program's
        // table 0x620B60's values, which its switch byte leaves unused)
        const float mid[12] = {0.6f, 0.8f, 0.5f, -0.1f, 0.05f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.02f, 3.06e-5f};
        for (u32 k = 0; k < 12; ++k) ee.wf(o.cond + 4 + 0x20 + 4 * k, mid[k]);
        const float a[4] = {-0.6f, -0.2f, 0.1f, 0.35f}, b[4] = {-0.5f, -0.1f, 0.2f, 0.6f};
        for (int i = 0; i < o.cars; ++i)
            for (u32 w = 0; w < 4; ++w) {
                const u32 W = o.car(i) + sim::layout::kWheel0 + sim::layout::kWheelSize * w;
                ee.wf(W + 0xAC, a[w] + 0.6f * float(m >> 2)), ee.wf(W + 0xA8, b[w] + 0.5f * float(m >> 2));
            }
    }
    if (std::getenv("GT4R_DUMP_BODY")) { // the suspension's and body's values past the engine's
        const u32 pb = o.params(0), F = o.car(0) + kF;
        for (u32 a = 0; a < 2; ++a)
            std::printf("  axle %u: mass +0x38 %g, hold droop +0x44 %g, +0x48 %g, +0x4C %g, +0x50 %g, +0x54 %g, tire drop P+0x2DC %g, radius %g\n", a,
                        double(ee.rf(pb + 0x308 + 0x58 * a + 0x38)), double(ee.rf(pb + 0x308 + 0x58 * a + 0x44)), double(ee.rf(pb + 0x308 + 0x58 * a + 0x48)),
                        double(ee.rf(pb + 0x308 + 0x58 * a + 0x4C)), double(ee.rf(pb + 0x308 + 0x58 * a + 0x50)), double(ee.rf(pb + 0x308 + 0x58 * a + 0x54)),
                        double(ee.rf(pb + 0x2DC + 4 * a)), double(ee.rf(pb + 0x2C4 + 4 * a)));
        std::printf("  P+0x20 %g P+0x24 %g roll centre P+0x1060 %g, 0x1076 %u 0x1077 %u 0x1084 %g, mass F+0x4D4 %g, F+0x4D8 %g, P+0x48 %g\n", double(ee.rf(pb + 0x20)),
                    double(ee.rf(pb + 0x24)), double(ee.rf(pb + 0x1060)), ee.r8(pb + 0x1076), ee.r8(pb + 0x1077), double(ee.rf(pb + 0x1084)), double(ee.rf(F + 0x4D4)),
                    double(ee.rf(F + 0x4D8)), double(ee.rf(pb + 0x48)));
    }
    if (std::getenv("GT4R_DUMP_WEAR")) {
        for (u32 off = 0; off < 0x6C; off += 4) std::printf("  wear +%02X %08X %g\n", off, ee.r32(o.cond + 4 + off), double(ee.rf(o.cond + 4 + off)));
    }
    if (std::getenv("GT4R_DUMP_POINTERS")) { // the parameter block's pointers into itself (its tables' arrays)
        const u32 pb = o.params(0);
        for (u32 off = 0; off < kParamsSize; off += 4) {
            const u32 v = ee.r32(pb + off);
            if (v >= pb && v < pb + kParamsSize) std::printf("  P+%04X -> P+%04X\n", off, v - pb);
        }
    }
    // What our session builds for the step beside the program's own: the race's per-surface rolling
    // resistance tables, the mass the step reads.
    {
        StepTap road{"race tables (ours as the session builds them)", 1, 0};
        const sim::rules::Tables mine = sim::rules::tables(program, inputs.statics);
        const sim::rules::RoadTables theirs = road_tables(ee);
        bool same = mine.road.rolling_curve.n == theirs.rolling_curve.n;
        for (int k = 0; k < theirs.rolling_curve.n && k < 8; ++k)
            same = same && mine.road.rolling_curve.x[size_t(k)].bits == theirs.rolling_curve.x[size_t(k)].bits &&
                   mine.road.rolling_curve.y[size_t(k)].bits == theirs.rolling_curve.y[size_t(k)].bits;
        for (int k = 0; k < theirs.rolling_curve.n && k < 8 && print; ++k)
            if (mine.road.rolling_curve.x[size_t(k)].bits != theirs.rolling_curve.x[size_t(k)].bits || mine.road.rolling_curve.y[size_t(k)].bits != theirs.rolling_curve.y[size_t(k)].bits)
                std::printf("  rolling curve point %d: the program's %08X %08X, ours %08X %08X\n", k, theirs.rolling_curve.x[size_t(k)].bits, theirs.rolling_curve.y[size_t(k)].bits,
                            mine.road.rolling_curve.x[size_t(k)].bits, mine.road.rolling_curve.y[size_t(k)].bits);
        for (size_t k = 0; k < 24; ++k) {
            if (mine.road.rolling_mu[k].bits != theirs.rolling_mu[k].bits) {
                same = false;
                if (print) std::printf("  rolling resistance of surface %zu: the program's %08X, ours %08X\n", k, theirs.rolling_mu[k].bits, mine.road.rolling_mu[k].bits);
            }
        }
        u8 sb[8];
        ee.read_bytes(sim::rules::kSlipBytes, sb, 8);
        const sim::rules::SlipBytes theirs_slip = sim::rules::slip_bytes(sb);
        same = same && theirs_slip.fade == mine.slip.fade && theirs_slip.gain_on == mine.slip.gain_on && theirs_slip.gain_base == mine.slip.gain_base &&
               theirs_slip.gain_knee == mine.slip.gain_knee;
        const sim::rules::SlipForceGlobals g = slip_globals(ee);
        same = same && g.steer_mode == mine.globals.steer_mode && g.camber_ref.bits == mine.globals.camber_ref.bits &&
               g.camber_gain.bits == mine.globals.camber_gain.bits && g.mean_on == mine.globals.mean_on;
        same = same && ee.r32(o.car(0) + kF + 0x4D4) == ee.r32(o.params(0) + 0x48);
        if (print && !same)
            std::printf("  curve n %d/%d slip %d globals %d/%d %08X/%08X %08X/%08X %d/%d mass %08X/%08X\n", mine.road.rolling_curve.n, theirs.rolling_curve.n,
                        int(theirs_slip.fade == mine.slip.fade && theirs_slip.gain_on == mine.slip.gain_on), int(g.steer_mode), int(mine.globals.steer_mode),
                        g.camber_ref.bits, mine.globals.camber_ref.bits, g.camber_gain.bits, mine.globals.camber_gain.bits, g.mean_on, mine.globals.mean_on,
                        ee.r32(o.car(0) + kF + 0x4D4), ee.r32(o.params(0) + 0x48));
        for (u32 k = 0; k < 4; ++k) {
            const u32 theirs_k = ee.r32(sim::rules::kBrakeHeatConsts + 4 * k);
            const u32 ours_k = (k == 0 ? mine.brake_heat.c0 : k == 1 ? mine.brake_heat.c1 : k == 2 ? mine.brake_heat.c2 : mine.brake_heat.c3).bits;
            if (theirs_k != ours_k) {
                same = false;
                if (print) std::printf("  brake heat constant %u: the program's %08X, ours %08X\n", k, theirs_k, ours_k);
            }
        }
        {
            const sim::rules::DriveTables a = drive_tables(ee), b = mine.drive;
            const bool dt = a.awd.fade_start.bits == b.awd.fade_start.bits && a.awd.fade_floor.bits == b.awd.fade_floor.bits &&
                            a.awd.negative_full == b.awd.negative_full && a.stepless.full.bits == b.stepless.full.bits &&
                            a.stepless.none.bits == b.stepless.none.bits && a.stepless.down.bits == b.stepless.down.bits &&
                            a.stepless.up.bits == b.stepless.up.bits && a.gears.mode == b.gears.mode && a.gears.down_throttle == b.gears.down_throttle &&
                            a.gears.down_lift == b.gears.down_lift && a.gears.down_brake == b.gears.down_brake;
            if (!dt && print) std::printf("  the driveline's tables differ\n");
            same = same && dt;
        }
        road.equal = same ? 1 : 0;
        result.taps.push_back(road);
        if (print) std::printf("  %-28s %s\n", road.name.c_str(), same ? "equal" : "differ");
    }
    { // The sliding measures' tables (0x366D68 at the race's start) against ours from the program's bytes.
        StepTap st{"sliding measures' tables (ours)", 1, 0};
        const sim::rules::SlideTables k = sim::rules::slide_tables(program);
        size_t off = 0;
        auto cmp = [&](u32 a, gt3r::ee::F v, const char* what, u32 g, u32 kind, u32 j) {
            if (ee.r32(a) == v.bits) return;
            ++off;
            if (print && off < 20) std::printf("  slide table %s [%u][%u] %u: the program's %08X, ours %08X\n", what, g, kind, j, ee.r32(a), v.bits);
        };
        for (u32 g = 0; g < 3; ++g)
            for (u32 kind = 0; kind < 4; ++kind) {
                const u32 o = sim::rules::kSlideCurves + 0x210 * g + 0x84 * kind;
                const sim::rules::SlideCurves& c = k.set[g][kind];
                if (s16(ee.r16(o + 0x20)) != c.a.n || s16(ee.r16(o + 0x6C)) != c.b.n) ++off;
                for (u32 j = 0; j < u32(c.a.n); ++j) cmp(o + 4 * j, c.a.x[j], "a.x", g, kind, j), cmp(o + 0x10 + 4 * j, c.a.y[j], "a.y", g, kind, j);
                for (u32 j = 0; j < u32(c.b.n); ++j) cmp(o + 0x2C + 4 * j, c.b.x[j], "b.x", g, kind, j), cmp(o + 0x4C + 4 * j, c.b.y[j], "b.y", g, kind, j);
                cmp(o + 0x78, c.base, "base", g, kind, 0), cmp(o + 0x7C, c.knee, "knee", g, kind, 0), cmp(o + 0x80, c.gain, "gain", g, kind, 0);
            }
        { // the sway's constants (0x34A4B8)
            u8 sb[10];
            ee.read_bytes(sim::rules::kSwayBytes, sb, 10);
            const std::array<gt3r::ee::F, 8> sk = sim::rules::sway_constants(sb);
            for (u32 j = 0; j < 8; ++j) cmp(0x6202C8 + 4 * j, sk[j], "sway", 0, 0, j);
        }
        { // the cues' table (0x34F6A0)
            const sim::rules::FeelTables ft = sim::rules::feel_tables(program);
            for (u32 j = 0; j < 32; ++j) cmp(sim::rules::kCueTable + 4 * j, ft.cue[j], "cue", 0, 0, j);
        }
        { // layout 7's motors' constants (0x355978, which the converter runs for every car of four wheel drive system 5)
            const sim::rules::MotorTables m = sim::rules::drive_tables(program).motors;
            const gt3r::ee::F w[25] = {m.front, m.rear, m.front_drive, m.rear_drive, m.front_regen, m.rear_regen, m.front_scale, m.capacity,
                                       m.front_charge_cap, m.rear_charge_cap, m.idle_rpm, m.idle_gain, m.idle_share, m.idle_power, m.low, m.high,
                                       m.reserve, m.front_share, m.drive_accel, m.drive_lat, m.drive_dead, m.regen_accel, m.regen_lat,
                                       m.regen_dead, m.flow_scale};
            if (ee.r32(sim::rules::kMotorTables) != 0)
                for (u32 j = 0; j < 25; ++j) cmp(sim::rules::kMotorTables + 4 * j, w[j], "motors", 0, 0, j);
        }
        const gt3r::ee::F loose[5] = {k.loose_scale, k.loose_x0, k.loose_y0, k.loose_x1, k.loose_y1};
        for (u32 j = 0; j < 5; ++j) cmp(sim::rules::kSlideLoose + 4 * j, loose[j], "loose", 0, 0, j);
        st.equal = off ? 0 : 1;
        result.taps.push_back(st);
        if (print) std::printf("  %-28s %s\n", st.name.c_str(), off ? "differ" : "equal");
    }
    const int steps = step_count > 0 ? step_count : std::getenv("GT4R_STEPS") ? std::atoi(std::getenv("GT4R_STEPS")) : 1200;
    try {
        for (int i = 0; i < steps; ++i) {
            o.pad[0] = drive(i);
            start_before_step(ee, o, i);
            orig_step(ee, o);
        }
    } catch (const std::exception& e) {
        char b[160];
        std::snprintf(b, sizeof b, "stopped: %s (pc %08X ra %08X)", e.what(), ee.cur_pc, ee.g32(31));
        result.error = b;
        if (print) std::printf("step: %s\n", b);
        return result;
    }
    if (print) {
        const u32 at = o.car(0);
        std::printf("step: %d steps of the program's own; the car at %.2f %.2f %.2f, %.1f km/h\n", steps, double(ee.rf(at + kF)), double(ee.rf(at + kF + 4)),
                    double(ee.rf(at + kF + 8)), double(ee.rf(at + kF + 0x53C)) * 3.6);
        taps.print();
        print_start_spans();
        if (collide_check) collide_check->print();
    }
    for (const TapStat& st : taps.stats()) result.taps.push_back({st.name, size_t(st.calls), size_t(st.clean)});
    start_spans([&](const std::string& name, u64 calls, u64 clean) { result.taps.push_back({name, size_t(calls), size_t(clean)}); });
    result.ran = true;
    return result;
}

} // namespace gt4r::diff
