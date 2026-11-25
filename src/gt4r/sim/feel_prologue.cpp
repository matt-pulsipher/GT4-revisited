// Gran Turismo 4 Prologue's phase 19 past its physics (feel_prologue.h).

#include "gt4r/sim/feel_prologue.h"

#include "gt4r/sim/program.h"
#include "gt4r/sim/program_prologue.h"

namespace gt4r::sim::prologue {

namespace {
const F kZero = F();
constexpr u32 kLeanOn = 0x5042B8, kRollOn = 0x5042B4;
} // namespace

rules::SpeedTables speed_tables(const Program& program, u8 special_row) {
    rules::SpeedTables t;
    t.lean_on = program.w(kLeanOn), t.roll_on = program.w(kRollOn);
    const u32 row = PrologueProgram::kSpecialBodies + 0x78 * (special_row < 3 ? special_row : 0);
    t.lean_row = {F::raw(program.w(row + 0x30)), F::raw(program.w(row + 0x34))};
    t.special_x10 = F::raw(program.w(row + 0x10));
    return t;
}

rules::FeelTables feel_tables(const Program& program) {
    rules::FeelTables t;
    t.view_heading_gain = F::raw(program.w(0x571C98)), t.view_heading_damp = F::raw(program.w(0x571CA0));
    return t;
}

void speeds(ex::Car& c, FeelPart& x, const rules::WheelLayout& l, const rules::SpeedTables& t, const ex::Condition& k) {
    // (the two-wheeler's lean term reads F + 0x480 with the switch 0x5042B8, 0 in the image: ❓ not followed)
    rules::hub_velocities(c, l, t, F());
    c.speed_abs = c.speed;
    // 0x3C5698: the odometer and the distance total from the signed speed (Gran Turismo 3's 2 m odometer, 8,000 km wrap).
    if (!(c.speed_abs < kZero)) {
        const F d = c.speed_abs * c.dt;
        c.odo_carry = ex::odometer_add(c.odometer, c.odo_carry + d);
        const F wrap = F::raw(0x4AF42400);
        const F total = c.dist_total + d;
        c.dist_total = total;
        if (wrap <= total) c.dist_total = total - wrap;
    }
    if (c.speed_abs < kZero) c.speed_abs = -c.speed_abs;
    // 0x3B9700, 0x3B1860, 0x3B1958: the engine's wear and oil by the step's distance.
    const F d = c.speed_abs * c.dt;
    c.engine_wear = c.engine_wear + d;
    if (k.e3 < c.engine_wear) c.engine_wear = k.e3;
    const F o = c.oil;
    c.oil = o + d;
    if (o < kZero && !(c.oil < kZero)) c.oil = -(-k.omax);
    if (k.omax < c.oil) c.oil = k.omax;
    if (c.finished == 0 && c.max_speed < c.speed_abs) c.max_speed = c.speed_abs;
    // 0x3C55F8: the speedometer.
    {
        const F s = c.speed_abs, v = x.speedo, step = c.dt * F::raw(0x42DE38E2);
        if (v < s) {
            x.speedo = v + step;
            if (s < x.speedo) x.speedo = s;
        } else {
            x.speedo = v - step;
            if (x.speedo < s) x.speedo = s;
        }
    }
    rules::free_wheels(c, l);
}

} // namespace gt4r::sim::prologue
