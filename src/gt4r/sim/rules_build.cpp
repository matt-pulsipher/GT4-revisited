// Gran Turismo 4's own part of a car's record from our spec (rules_build.h).

#include "gt4r/sim/rules_build.h"

#include "gt4r/sim/rules_drive.h"
#include "gt4r/sim/rules_tires.h"

namespace gt4r::sim {

namespace {
using gt3r::ee::F;
using gt3r::ee::int_to_f;
const F kOne = F::raw(0x3F800000), kPercent = F::raw(0x3C23D70A), kKmh = F::raw(0x3E8E38E3);
const F kDegree = F::raw(0x3C8EFA34), k255 = F::raw(0x437F0000), k200 = F::raw(0x43480000);

// 0x353800: the slip angle of curve A's peak: for a tire read at its sideways speed, the axis point of
// the greatest value; otherwise the whole degree 0 .. 45 where sign x curve A at the folded angle is
// greatest.
F peak_slip(const rules::Tire& t) {
    if (t.direct_lateral) {
        F best = F(), at = F();
        for (int k = 0; k < t.curve_a.n && k < 8; ++k)
            if (best < t.curve_a.y[size_t(k)]) best = t.curve_a.y[size_t(k)], at = t.curve_a.x[size_t(k)];
        return at;
    }
    F best = F();
    s32 deg = 90;
    for (s32 d = 0; d < 0x2E; ++d) {
        F sign;
        const F v = gt3r::sim::exact::slip_angle_fold(rules::behaviour(), int_to_f(d) * kDegree, sign);
        const F y = rules::table(t.curve_a, v) * sign;
        if (best < y) best = y, deg = d;
    }
    return int_to_f(deg) * kDegree;
}
// The engine and driveline blocks (0x34EF30, 0x34ACA8): the engine's converter's, in Gran Turismo 4's places.
void drive_blocks(const Spec& s, const Program& program, const gt3r::sim::exact::Params& p, rules::DriveExtra& x) {
    (void)s, (void)program;
    rules::EngineBlock& e = x.e;
    e.torque = {};
    e.torque.n = p.e.torque.n;
    for (size_t k = 0; k < 16; ++k) e.torque.x[k] = p.e.torque.x[k], e.torque.y[k] = p.e.torque.y[k];
    e.rev_limit = p.e.rev_limit, e.idle = p.e.idle, e.friction = p.e.friction;
    e.turbo_knee = p.e.turbo_knee, e.turbo_shape = p.e.turbo_shape, e.power_cap_on = p.e.power_cap_on;
    e.power_cap = p.e.power_cap, e.power_cap_soft = p.e.power_cap_soft;
    e.turbo_peak = p.e.turbo_peak, e.turbo_rate = p.e.turbo_rate, e.turbo_coef = p.e.turbo_coef, e.boost_max = p.e.boost_max;
    rules::DrivelineBlock& d = x.d;
    d = {};
    d.layout = p.d.layout, d.unequal = p.d.unequal, d.gears = p.d.gears, d.driven_axle = p.d.driven_axle;
    d.awd_k = p.d.awd_k, d.awd_capacity = p.d.awd_capacity, d.engine_inertia = p.d.engine_inertia;
    d.diff_type = p.d.diff_type, d.kind = p.d.kind;
    d.diff_initial = p.d.diff_initial, d.diff_accel = p.d.diff_accel, d.diff_decel = p.d.diff_decel;
    d.launch_rpm = p.d.launch_rpm, d.up_rpm = p.d.up_rpm;
    for (size_t g = 0; g < 6; ++g) d.down_rpm[g] = p.d.down_rpm[g];
    for (size_t g = 0; g < 8; ++g) d.ratio[g] = p.d.ratio[g], d.gear_speed[g] = p.d.gear_speed[g];
    d.tire_radius = p.tire_radius, d.inv_tire_radius = p.inv_tire_radius, d.width = p.width;
    d.shift_clutch_torque = p.d.shift_clutch_torque, d.wheel_gain = p.d.wheel_gain, d.axle_inertia = p.d.axle_inertia;
    d.inv_axle_inertia = p.d.inv_axle_inertia, d.inv_engine_inertia = p.d.inv_engine_inertia;
}
} // namespace

u8 wheel_layout_index(const Spec& s) { return s.b(0x02); }

void gt4_extras(const Spec& s, const Program& program, Record& r) {
    const gt3r::sim::exact::Params& p = r.car.p;
    rules::TireExtra& t = r.x.tires;
    r.layout = rules::wheel_layout(program, wheel_layout_index(s));
    // The tire index per surface (0x361570): the surface's block class (spec +0x1F0 + s; 0 past 2),
    // the rear's records three on.
    for (u32 sf = 0; sf < 24; ++sf) {
        u8 k = s.b(0x1F0 + sf);
        if (k >= 3) k = 0;
        t.tire_index[0][sf] = k, t.tire_index[1][sf] = u8(k + 3);
    }
    // The records (0x361570 / 0x3613D8): the block of the record's own index, or with spec +0x1E0 0 the
    // front's for the rear too. The curves are the engine's converter's (the same rules); the grips,
    // the wear's floor and the three kind bytes from the block.
    for (int k = 0; k < 6; ++k) {
        rules::Tire& o = t.tire[size_t(k)];
        const gt3r::sim::exact::TireRecord& e = p.tire[size_t(k)];
        const int blk = s.b(0x1E0) ? k : k % 3;
        const u32 b = Spec::tire_block(blk);
        o.curve_c = e.curve_c, o.slip_drive = e.slip_drive, o.slip_brake = e.slip_brake, o.brake_peak = e.brake_peak, o.curve_d = e.curve_d;
        // Curves A and B (0x361220): the first point's bytes read as 0; A's angles span the block's +0x02
        // degrees (90 when 0, at most pi / 2), B's pi / 2.
        {
            const u8 deg = s.b(b + 2) ? s.b(b + 2) : 90;
            F range = int_to_f(deg) * kDegree;
            if (F::raw(0x3FC90FDA) < range) range = F::raw(0x3FC90FDA);
            o.curve_a = {};
            o.curve_a.n = s.b(b + 8);
            for (u32 i = 0; i < u32(o.curve_a.n) && i < 8; ++i) {
                const u8 xb = i ? s.b(b + 0x10 + i) : 0, yb = i ? s.b(b + 0x18 + i) : 0;
                o.curve_a.x[i] = int_to_f(xb) * range / k255, o.curve_a.y[i] = int_to_f(yb) / k200;
            }
            o.curve_b = {};
            o.curve_b.n = s.b(b + 0x0C);
            for (u32 i = 0; i < u32(o.curve_b.n) && i < 6; ++i) {
                const u8 xb = i ? s.b(b + 0x50 + i) : 0, yb = i ? s.b(b + 0x56 + i) : 0;
                o.curve_b.x[i] = int_to_f(xb) * F::raw(0x3FC90FD9) / k255, o.curve_b.y[i] = int_to_f(yb) / k200;
            }
            if (o.curve_b.n == 0) o.curve_b = e.curve_b; // (the program's default curve)
        }
        // The load curve (0x3613D8): loads of the block's bytes x 20 x 9.8 N, values / 200.
        o.load_curve = {};
        o.load_curve.n = s.b(b + 0x0B);
        for (u32 i = 0; i < u32(o.load_curve.n) && i < 4; ++i)
            o.load_curve.x[i] = (int_to_f(s.b(b + 0x5C + i)) * F::raw(0x41A00000)) * F::raw(0x411CCCCC), o.load_curve.y[i] = int_to_f(s.b(b + 0x60 + i)) / k200;
        if (o.load_curve.n == 0) o.load_curve = e.load_curve;
        // The base grip: 9.8 x curve D at no slip (0x359568 on curve C's axis).
        o.grip = gt3r::sim::exact::lookup_bisect(o.curve_c.n, o.curve_c.x.data(), o.curve_d.data(), F()) * F::raw(0x411CCCCC);
        const F g = int_to_f(s.b(b) ? s.b(b) : 100) * kPercent;
        for (u32 sf = 0; sf < 24; ++sf) {
            const u8 v = s.b(b + 0x64 + sf);
            o.mu[sf] = v ? int_to_f(v) * kPercent * g : g;
        }
        o.wear_rate = int_to_f(s.b(b + 1)) * kPercent;
        const F floor = int_to_f(program.b(Program::kStatic + 0xBF + u32(blk % 3))) * kPercent;
        o.wear_floor = floor / g;
        if (kOne < o.wear_floor) o.wear_floor = kOne;
        o.direct_slip = s.b(b + 3), o.direct_lateral = s.b(b + 4), o.x1da = s.b(b + 5);
        F pk = peak_slip(o);
        if (F::raw(0x3F490FD9) < pk) pk = F::raw(0x3F490FD9);
        o.peak_slip = pk;
    }
    for (size_t a = 0; a < 2; ++a) {
        t.width[a] = int_to_f(s.b(0x1C8 + u32(a)) * 5) * F::raw(0x3A83126E);
        t.axle_grip[a] = int_to_f(s32(s.b(0x2A + u32(a))) + 900) * F::raw(0x3A83126E);
        // The camber curves (0x361908): angles (0x80 - byte) x 4 degrees / 32, from the last point back;
        // values / 200.
        auto& cg = t.camber_grip[a];
        cg = {};
        cg.n = s.b(0x1CE + u32(a));
        for (u32 i = 0; i < u32(cg.n) && i < 4; ++i) {
            const u32 k = u32(cg.n) - 1 - i;
            cg.x[k] = (int_to_f(0x80 - s32(s.b(0x1D0 + 4 * u32(a) + i))) * F::raw(0x3D8EFA34)) * F::raw(0x3D000000);
            cg.y[k] = int_to_f(s.b(0x1D8 + 4 * u32(a) + i)) / k200;
        }
    }
    // The direct slip ratio's and lateral speed's scales (0x361908): 1 and pi / 2 over the static
    // block's speeds +0x118 / +0x119 (km/h; 50 when 0).
    auto speed = [&](u32 o) {
        const u8 v = program.b(Program::kStatic + o);
        return int_to_f(v ? v : 50) * kKmh;
    };
    // Each record's base grip by its load curve at its axle's static load per wheel (0x361798): half the
    // axle's static load plus the unsprung weight, over the axle's width.
    for (int k = 0; k < 6; ++k) {
        rules::Tire& o = t.tire[size_t(k)];
        if (o.grip == F()) continue;
        const size_t a = size_t(k / 3);
        // (the unsprung weight as the program's suspension builder 0x360B60 makes it: the spec's kilograms
        // x 0.25 x 9.8)
        const F unsprung = (int_to_f(s.b((a ? 0x70u : 0x58u) + 3)) * F::raw(0x3E800000)) * F::raw(0x411CCCCC);
        const F load = (a ? p.xe58 : p.xe54) * F::raw(0x3F000000) + unsprung;
        o.grip = o.grip * rules::table(o.load_curve, load / t.width[a]);
    }
    t.direct_lateral_scale = F::raw(0x3FC90FD9) / speed(0x118);
    t.direct_slip_scale = kOne / speed(0x119);
    r.x.chassis.mass = p.mass;
    drive_blocks(s, program, p, r.x.drive);
}

} // namespace gt4r::sim
