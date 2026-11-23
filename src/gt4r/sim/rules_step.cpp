// Gran Turismo 4's routines in a session's step (rules_step.h).

#include "gt4r/sim/rules_step.h"


namespace gt4r::sim::rules {

Tables tables(const Program& program, const ex::Statics&) {
    Tables t;
    u8 b[8] = {};
    for (u32 i = 0; i < 8; ++i) b[i] = program.b(kSlipBytes + i);
    t.slip = slip_bytes(b);
    // The rolling resistance's curve (0x35F808): the static block's +0x0B points, speeds 2 x the bytes at
    // +0x0C km/h, values the bytes at +0x14 x 0.001.
    t.road.rolling_curve = {};
    t.road.rolling_curve.n = program.b(Program::kStatic + 0x0B);
    for (u32 k = 0; k < u32(t.road.rolling_curve.n) && k < 8; ++k) {
        t.road.rolling_curve.x[k] = gt3r::ee::int_to_f(s32(program.b(Program::kStatic + 0x0C + k)) * 2) * F::raw(0x3E8E38E3);
        t.road.rolling_curve.y[k] = gt3r::ee::int_to_f(program.b(Program::kStatic + 0x14 + k)) * F::raw(0x3A83126E);
    }
    // Each surface's factor: the static block's s16 at +0x34 + 2 s, x 0.01.
    for (u32 s = 0; s < 24; ++s)
        t.road.rolling_mu[s] = gt3r::ee::int_to_f(gt3r::s16(program.h(Program::kStatic + 0x34 + 2 * s))) * F::raw(0x3C23D70A);
    // The steering mode and the mean slip angle's switch as a race's set-up leaves them (both 1 after the
    // program's own set-up in tools/gt4diff; the image holds 0: ❓ the routine that sets them).
    t.globals.steer_mode = 1;
    t.globals.camber_ref = F::raw(program.w(kCamberRef)), t.globals.camber_gain = F::raw(program.w(kCamberGain));
    t.globals.mean_on = 1;
    t.drive = drive_tables(program);
    // The brake heat's constants (0x3F3808): 1 / (b0 x 9800), 1 / (b1 x 0.1 x 100), b2 km/h in m/s, b3 x 0.01, of the
    // static block's bytes +0xAC .. +0xAF (the first two 1 when 0).
    {
        auto byte = [&](u32 o, bool least) { const u8 v = program.b(Program::kStatic + o); return least && v == 0 ? u8(1) : v; };
        const F one = F::raw(0x3F800000);
        t.brake_heat.c0 = one / (gt3r::ee::int_to_f(byte(0xAC, true)) * F::raw(0x46191FFF));
        t.brake_heat.c1 = one / ((gt3r::ee::int_to_f(byte(0xAD, true)) * F::raw(0x3DCCCCCC)) * F::raw(0x42C80000));
        t.brake_heat.c2 = gt3r::ee::int_to_f(byte(0xAE, false)) * F::raw(0x3E8E38E3);
        t.brake_heat.c3 = gt3r::ee::int_to_f(byte(0xAF, false)) * F::raw(0x3C23D70A);
    }
    return t;
}

void prep_tires(ex::Car& c, Extra& x, const WheelLayout& l, const Tables& t, const ex::Condition& cond) {
    engine_torques(c, x.drive, cond);
    const bool wear = wear_grip(c, x.wear, x.tires, l, t.wear);
    peak_force(c, x.tires, l, wear);
    slip_angles(c, l);
}

void dynamics(ex::Car& c, Extra& x, const WheelLayout& l, const Tables& t) {
    brakes_and_clutch(c, x.drive, l);
    rolling_resistance(c, l, t.road);
    tire_curves(c, x.tires, l);
    tire_long_force(c, l, t.slip);
    tire_slip_force(c, x.tires, l, t.globals);
    forward_step(c, x.chassis, x.tires, l);
    wheel_torques(c, x.drive, l);
    vectoring(c, x.drive, l);
    axle_step(c, x.drive, l, t.drive);
    wheel_speeds(c, x.drive, l);
    engine_speed(c, x.drive, l);
    gear_step(c, x.drive, l, x.tires, t.drive);
    const bool air = airborne(c, l);
    lateral_yaw(c, x.chassis, x.tires, l, air, t.globals);
    stop_hold(c, l, air);
}

} // namespace gt4r::sim::rules
