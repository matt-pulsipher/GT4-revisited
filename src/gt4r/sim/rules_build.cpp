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
    e.rev_limit = p.e.rev_limit, e.idle = p.e.idle;
    e.power_cap_on = p.e.power_cap_on, e.power_cap = p.e.power_cap, e.power_cap_soft = p.e.power_cap_soft;
    // The turbo (0x34ED48): the cap (+0x113 x 0.1) with a first stage's knee (+0x116, x 100 rpm), peak and rate
    // (+0x118 / +0x11A, x 0.1), a second's when all three of its bytes (+0x117, +0x119, +0x11B) are set; each
    // stage's shape below the knee (+0x114 / +0x115, 0 .. 2) and its square term (the peak over the knee squared).
    {
        const F tenth = F::raw(0x3DCCCCCC);
        e.boost_max = s.b(0x113) ? int_to_f(s.b(0x113)) * tenth : F();
        if (!s.b(0x116) || !s.b(0x118) || !s.b(0x11A)) e.boost_max = F();
        e.turbo_knee = {}, e.turbo_shape = {}, e.turbo_peak = {}, e.turbo_rate = {}, e.turbo_coef = {};
        if (!(e.boost_max == F())) {
            for (u32 st = 0; st < 2; ++st) {
                if (st == 1 && !(s.b(0x117) && s.b(0x119) && s.b(0x11B))) break;
                e.turbo_shape[st] = s.b(0x114 + st) < 3 ? s.b(0x114 + st) : 0;
                e.turbo_peak[st] = int_to_f(s.b(0x118 + st)) * tenth;
                e.turbo_rate[st] = int_to_f(s.b(0x11A + st)) * tenth;
                e.turbo_knee[st] = gt3r::s16(s32(s.b(0x116 + st)) * 100);
                const F k = int_to_f(e.turbo_knee[st]);
                e.turbo_coef[st] = e.turbo_peak[st] / (k * k);
            }
        }
    }
    // The torque curve (0x34EF30): the spec's points (+0xBF), speeds the bytes at +0xC0 x 100 rpm, torques the
    // u16s at +0xD8 (the last, when 0, the one before it) x 0.01 x 9.8 N m times the output factor (+0x108 per
    // mille) and the power percentage (+0x07), shaded linearly over the curve from the low end's factor (+0x10C
    // percent) to the high end's (+0x10A per mille); each point then x the capped boost over the uncapped.
    e.torque = {};
    const u32 n = s.b(0xBF) < 24 ? s.b(0xBF) : 24;
    e.torque.n = gt3r::s16(n);
    auto torque_word = [&](u32 k) {
        u16 v = s.h(0xD8 + 2 * k);
        if (k + 1 == n && v == 0 && k > 0) v = s.h(0xD8 + 2 * (k - 1));
        const s32 t = gt3r::s16(v) < -1 ? s32(v) : s32(gt3r::s16(v));
        return t;
    };
    for (u32 k = 0; k < n; ++k) e.torque.x[k] = int_to_f(s32(s.b(0xC0 + k)) * 100) * F::raw(0x3DD6774E);
    if (n > 0) {
        const F low = s.b(0x10C) ? int_to_f(s.b(0x10C)) * kPercent : kOne;
        const F high = s.h(0x10A) ? int_to_f(s.h(0x10A)) * F::raw(0x3A83126E) : kOne;
        F out = s.h(0x108) ? int_to_f(s.h(0x108)) * F::raw(0x3A83126E) : kOne;
        out = out * kPercent;
        if (const u8 pw = s.b(0x07); pw != 0 && pw != 100) out = out * (int_to_f(pw) / F::raw(0x42C80000));
        const F x0 = e.torque.x[0], span = e.torque.x[n - 1] - x0, shade = high - low;
        for (u32 k = 0; k < n; ++k) {
            const F f = low + (shade * (e.torque.x[k] - x0)) / span;
            e.torque.y[k] = (int_to_f(torque_word(k)) * F::raw(0x411CCCCC)) * (out * f);
        }
        // The friction (+0xD0): the spec's u16 +0xBA x 0.1 x 9.8 over the curve's last speed (x 0.9 instead when the
        // static block's byte +0x13C is set).
        const F fr = (int_to_f(s.h(0xBA)) * F::raw(0x3DCCCCCC)) * F::raw(0x411CCCCC);
        e.friction = program.b(Program::kStatic + 0x13C) ? fr * F::raw(0x3F666666) : fr / e.torque.x[n - 1];
        for (u32 k = 0; k < n; ++k) {
            const gt3r::s16 rpm = gt3r::s16(s32(s.b(0xC0 + k)) * 100);
            F sum = F();
            for (u32 st = 0; st < 2; ++st)
                if (!(e.turbo_peak[st] == F())) sum = sum + rules::turbo_boost(e, rpm, int(st));
            const F raw = sum + kOne;
            F capped = raw;
            if (e.boost_max + kOne < capped) capped = e.boost_max + kOne;
            e.torque.y[k] = e.torque.y[k] * (capped / raw);
        }
    }
    rules::DrivelineBlock& d = x.d;
    d = {};
    d.layout = p.d.layout, d.unequal = p.d.unequal, d.gears = p.d.gears, d.driven_axle = p.d.driven_axle;
    d.awd_k = p.d.awd_k, d.awd_capacity = p.d.awd_capacity, d.engine_inertia = p.d.engine_inertia;
    d.diff_type = p.d.diff_type, d.kind = p.d.kind;
    d.diff_initial = p.d.diff_initial, d.diff_accel = p.d.diff_accel, d.diff_decel = p.d.diff_decel;
    d.launch_rpm = p.d.launch_rpm, d.up_rpm = p.d.up_rpm;
    d.tire_radius = p.tire_radius, d.inv_tire_radius = p.inv_tire_radius, d.width = p.width;
    d.shift_clutch_torque = p.d.shift_clutch_torque, d.wheel_gain = p.d.wheel_gain, d.axle_inertia = p.d.axle_inertia;
    d.inv_axle_inertia = p.d.inv_axle_inertia, d.inv_engine_inertia = p.d.inv_engine_inertia;
    // Gran Turismo 4's own rules of the driveline builder (0x34ACA8) on those values: the gearbox's kind (spec +0x158;
    // 5 is 2, 6 is 0; a stepless box of kind 1 needs two gears, a one-speed box has one), the gears (at most 9),
    // the axles' tires told apart (layout 5 with unequal radii, or a second reduction of kind 4), the launch speed
    // (+0x160 x 10 rpm; at least idle + 100, a stepless box's idle), the up-shift speed (+0xBE x 100), the
    // down-shift speeds, the stepless ratios relative to the first, the class of the up-shift speed.
    // The drive (0x34BC98): spec +0x128 0 (and 3, 4) the rear wheels, 1 the front, 2 all four by the system +0x129:
    // 0 layout 2 (the front driven), 1 layout 3, 2 layout 4, 3 layout 5, 4 layout 6, 5 layout 7, 6 layout 9 (the front
    // driven), 7 layout 8, 8 layout 10; the split the front share (+0x12A, percent), for layout 4 1 from 50 on, for
    // layout 5 x 0.36 x the program's byte 0x620308 percent, for layout 6 1 / (the share in degrees); the center
    // clutch's capacity +0x12B x 10 x 9.8 N m (8 for layout 4, 15 for layout 6 when 0; layout 5's the program's byte
    // 0x620309).
    {
        const u8 drive = s.b(0x128), sys = s.b(0x129);
        const F share = int_to_f(s.b(0x12A)) * kPercent;
        auto capacity = [&](u8 fallback) { return int_to_f(s32(s.b(0x12B) ? s.b(0x12B) : fallback) * 10) * F::raw(0x411CCCCC); };
        d.awd_k = F(), d.awd_capacity = F();
        if (drive == 1) d.layout = 1, d.driven_axle = 0;
        else if (drive != 2 || sys > 8) d.layout = drive == 2 ? 3 : 0, d.driven_axle = 1;
        else {
            d.awd_k = share, d.driven_axle = 1;
            switch (sys) {
            case 0: d.layout = 2, d.driven_axle = 0; break;
            case 1: d.layout = 3; break;
            case 2:
                d.layout = 4;
                if (!(s.b(0x12A) < 50)) d.awd_k = kOne;
                d.awd_capacity = capacity(8);
                break;
            case 3:
                d.layout = 5;
                d.awd_k = (share * F::raw(0x3EB851ED)) * (int_to_f(program.b(0x620308)) / F::raw(0x42C80000));
                d.awd_capacity = int_to_f(s32(program.b(0x620309)) * 10) * F::raw(0x411CCCCC);
                break;
            case 4:
                d.layout = 6;
                if (!(share == F())) d.awd_k = kOne / ((share * F::raw(0x42C80000)) * F::raw(0x3C8EFA34));
                d.awd_capacity = capacity(15);
                break;
            case 5: d.layout = 7; break; // (0x355978: the motors' set-up, not ported)
            case 6: d.layout = 9, d.driven_axle = 0; break;
            case 7: d.layout = 8; break;
            default: d.layout = 10; break;
            }
        }
    }
    u8 kind = s.b(0x158);
    if (kind == 5) kind = 2;
    else if (kind == 6) kind = 0;
    u8 gears = s.b(0x155);
    if (gears >= 10) gears = 9;
    if (kind == 3 && gears != 1) gears = 1;
    if (kind == 1 && gears < 2) kind = 0;
    d.kind = kind;
    d.gears = gears;
    // The ratios (0x34ACA8): each gear's word (spec +0x138 + 2 g, x 0.001; reverse and first 3.5 and 4.0 when below
    // 100; a later gear below 100 ends the box there) times the final drive (+0x150, x 0.001; 4.0 below 500), reverse
    // negative; then the second reduction (+0x152 x 0.001, from 500) by its kind (+0x154): 1 every gear times it, 2
    // reverse and the gears from the fifth on, 3 reverse and the third and fourth, each times it over the final drive.
    d.ratio = {};
    {
        const gt3r::s16 fw = gt3r::s16(s.h(0x150));
        const F fin = int_to_f(fw < 500 ? 4000 : fw) * F::raw(0x3A83126E);
        for (u32 g = 0; g <= d.gears && g < 10; ++g) {
            const gt3r::s16 r = gt3r::s16(s.h(0x138 + 2 * g));
            s32 v = r;
            if (r < 100) {
                if (g >= 2) {
                    d.gears = u8(g - 1);
                    break;
                }
                v = g == 1 ? 4000 : 3500;
            }
            d.ratio[g] = (int_to_f(v) * F::raw(0x3A83126E)) * fin;
        }
        d.ratio[0] = -d.ratio[0];
        const gt3r::s16 rw = gt3r::s16(s.h(0x152));
        const u8 rk = s.b(0x154);
        if (rk != 0 && rk != 4 && !(rw < 500)) {
            F f = int_to_f(rw) * F::raw(0x3A83126E);
            if (rk == 1) {
                for (u32 g = 0; g <= d.gears && g < 10; ++g) d.ratio[g] = d.ratio[g] * f;
            } else if (rk == 2 || rk == 3) {
                f = f / fin;
                d.ratio[0] = d.ratio[0] * f;
                if (rk == 2)
                    for (u32 g = 5; g <= d.gears && g < 10; ++g) d.ratio[g] = d.ratio[g] * f;
                else
                    d.ratio[3] = d.ratio[3] * f, d.ratio[4] = d.ratio[4] * f;
            }
        }
        // The engine's inertia (+0xB8 x 0.001) and its inverse; each gear's speed factor: its ratio over the driven
        // axle's tire radius.
        d.engine_inertia = int_to_f(s.h(0xB8)) * F::raw(0x3A83126E);
        if (s.h(0xB8)) d.inv_engine_inertia = kOne / d.engine_inertia;
        d.gear_speed = {};
        for (u32 g = 0; g <= d.gears && g < 10; ++g) d.gear_speed[g] = d.ratio[g] / d.tire_radius[d.driven_axle & 1];
    }
    d.unequal = (d.layout == 5 && !(d.tire_radius[0] == d.tire_radius[1])) || s.b(0x154) == 4 ? 1 : 0;
    const bool stepless = u8(kind - 1) < 2;
    d.launch_rpm = u16(s32(s.b(0x160)) * 10);
    d.up_rpm = u16(s32(s.b(0xBE)) * 100);
    if (stepless) d.launch_rpm = x.e.idle;
    else if (s32(d.launch_rpm) - 100 < s32(x.e.idle)) d.launch_rpm = u16(x.e.idle + 100);
    for (u32 g = 1; g < d.gears && g + 1 < 10; ++g) {
        const F turn = int_to_f(s32(d.up_rpm)) * F::raw(0x3DD6774E);
        s32 v = gt3r::ee::to_int((((turn * d.ratio[g + 1]) / d.ratio[g]) * F::raw(0x42700000)) / F::raw(0x40C90FDA));
        if (program.b(Program::kStatic + 0x112) && g == 1) v = (s32(d.launch_rpm) + v) / 2;
        v -= gt3r::ee::to_int(((d.gear_speed[g + 1] * F::raw(0x4031C71B)) * F::raw(0x42700000)) / F::raw(0x40C90FDA));
        const s32 least = s32(d.launch_rpm) + 500 * s32(g);
        d.down_rpm[g - 1] = u16(v < least ? least : v);
    }
    if (stepless && d.gears >= 2)
        for (u32 g = 2; g <= d.gears && g < 10; ++g) d.ratio[g] = d.ratio[g] / d.ratio[1];
    d.x13 = d.up_rpm < 4500 ? 1 : d.up_rpm < 5000 ? 2 : 3;
    if (s.b(0x01) == 5) d.x13 = 4;
}
// The suspension's and body's part (0x361908, 0x360B60, 0x34BA30).
void body_extras(const Spec& s, const gt3r::sim::exact::Params& p, const rules::WheelLayout& l, rules::BodyExtra& b) {
    const F kQuarter = F::raw(0x3E800000), kMilli = F::raw(0x3A83126E), kInch = F::raw(0x3CD013A9), kHalfInch = F::raw(0x3C5013A9);
    const F kHalf = F::raw(0x3F000000), kTwentieth = F::raw(0x3D4CCCCC);
    for (u32 a = 0; a < 2; ++a) {
        rules::AxleBody& x = b.axle[a];
        // The wheel's own mass: the spec's unsprung kilograms x 0.25 (0x360B60).
        x.wheel_mass = int_to_f(s.b((a ? 0x70u : 0x58u) + 3)) * kQuarter;
        x.linear_bump_max = F(), x.linear_rebound_max = F(); // (the linear damper fit's; ❓ their builder: not used, 0x620DD9 is 0)
        // The tire's sidewall (0x34BA30): width x aspect; less a rim made larger by the chassis (tire kinds 6 .. 10), at least half an inch.
        const u8 wb = s.b(0x1C8 + a), rim = s.b(0x1CA + a), aspect = s.b(0x1CC + a);
        const F width = int_to_f(wb ? s32(wb) * 5 : 10) * kMilli;
        F half;
        if (rim < 61) half = (int_to_f(rim) * kInch) * kHalf;
        else if (rim % 10 == 0) half = (int_to_f(rim / 10) * kInch) * kHalf;
        else half = (int_to_f(rim) * kInch) * kTwentieth;
        F drop = (width * int_to_f(aspect)) * kTwentieth;
        const gt3r::s8 bigger = gt3r::s8(s.b(0x1E2 + a));
        if (bigger != 0) {
            F more = (int_to_f(bigger) * kInch) * kHalf;
            if (half + more < kInch) more = kInch - half;
            drop = drop - more;
            if (drop < kHalfInch) drop = kHalfInch;
        }
        b.tire_drop[a] = drop;
    }
    // The droops while the hold counter runs (0x362D48): each axle's droop, the one whose hub hangs higher
    // over it moved down to the other's.
    {
        const gt3r::sim::exact::Suspension &f = p.susp[0], &r = p.susp[1];
        b.axle[0].hold_droop = f.droop, b.axle[1].hold_droop = r.droop;
        const F df = f.offset - f.droop, dr = r.offset - r.droop;
        if (dr < df) b.axle[1].hold_droop = r.droop - (df - dr);
        else if (df < dr) b.axle[0].hold_droop = f.droop - (dr - df);
    }
    b.front_share = F::raw(0x3F800000) - p.rear_weight;
    b.roll_centre = int_to_f(s.b(0x27)) * kMilli;
    b.speed_part = 0, b.speed_part_kind = 0, b.speed_part_lift = F(); // (the part that changes with speed: not built)
    // The sprung mass (0x362B58): the mass less each axle's wheels' own.
    const F nf = int_to_f(l.axle_wheels[0]), nr = int_to_f(l.axle_wheels[1]);
    if (l.axle_wheels[0] == 2 && l.axle_wheels[1] == 2) {
        const F sum = b.axle[0].wheel_mass + b.axle[1].wheel_mass;
        b.sprung_mass = p.mass - (sum + sum);
    } else b.sprung_mass = p.mass - (b.axle[0].wheel_mass * nf + b.axle[1].wheel_mass * nr);
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
    body_extras(s, p, r.layout, r.x.body);
}

} // namespace gt4r::sim
