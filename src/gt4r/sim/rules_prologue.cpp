// Gran Turismo 4 Prologue's own race step (rules_prologue.h).

#include "gt4r/sim/rules_prologue.h"

#include "base/ee_math.h"

#include <cstdlib>
#include "gt4r/sim/program.h"
#include "gt4r/sim/program_prologue.h"

namespace gt4r::sim::prologue {

namespace {
const F kOne = F::raw(0x3F800000), kMinusOne = F::raw(0xBF800000);
const F kKmh = F::raw(0x3E8E38E3);                                // 1 / 3.6
const F kHalfPi = F::raw(0x3FC90FD9);

F speed_of(u8 kmh) { return gt3r::ee::int_to_f(kmh ? kmh : 50) * kKmh; }
} // namespace

Tables tables(const Program& program) {
    Tables t;
    const PrologueProgram p = PrologueProgram::locate(program);
    if (!p.recognized()) return t;
    t.found = true;
    const u32 s = PrologueProgram::kStatic;
    u8 b[8];
    for (u32 k = 0; k < 8; ++k) b[k] = program.b(s + 0x84 + k);
    t.slip = rules::slip_bytes(b);
    for (u32 k = 0; k < 3; ++k) t.direct_speed[k] = program.b(s + 0xE4 + k);
    t.globals.camber_ref = F::raw(program.w(PrologueProgram::kCamberRef));
    t.globals.camber_gain = F::raw(program.w(PrologueProgram::kCamberGain));
    // (the image has 0; the race's set-up leaves it 1, ❓ its writer: a block 0x506610 the computer drivers' routines read)
    t.globals.mean_on = 1;
    {
        rules::SteerTables& g = t.steer;
        g.mode = program.b(s + 0xB9), g.scale = program.b(s + 0xBA);
        for (u32 k = 0; k < 2; ++k) {
            const u8 lim = program.b(s + 0xBB + k);
            if (lim == 0) g.mode = 0;
            g.limit[k] = gt3r::ee::int_to_f(lim) * F::raw(0x3AE4C385);
            g.spring[k] = gt3r::ee::int_to_f(program.b(s + 0xBD + k)) * F::raw(0x41200000);
            g.damping[k] = gt3r::ee::int_to_f(program.b(s + 0xBF + k));
        }
        for (u32 f = 0; f < 24; ++f) g.fixed_aim[f] = f < 16 ? program.b(PrologueProgram::kFixedAim + f) : 0;
        t.globals.steer_mode = g.mode;
    }
    for (u32 r = 0; r < t.wheel_layouts.size(); ++r)
        for (u32 k = 0; k < rules::kWheelLayoutSize; ++k) t.wheel_layouts[r][k] = program.b(PrologueProgram::kWheelLayouts + rules::kWheelLayoutSize * r + k);
    return t;
}

rules::WheelLayout wheel_layout(const Tables& t, u8 row) {
    if (!t.found || row >= t.wheel_layouts.size()) return {};
    return rules::wheel_layout(t.wheel_layouts[row].data());
}

rules::TireExtra tire_extra(const ex::Params& p, const TireKinds& kinds, const Tables& t) {
    rules::TireExtra x;
    for (size_t r = 0; r < 6; ++r) {
        const ex::TireRecord& a = p.tire[r];
        rules::Tire& o = x.tire[r];
        o.grip = a.grip, o.wear_rate = a.wear_rate;
        for (size_t s = 0; s < 16; ++s) o.mu[s] = a.mu[s];
        o.wear_floor = a.mu[11];
        o.curve_a = a.curve_a, o.peak_slip = a.peak_slip, o.curve_b = a.curve_b, o.curve_c = a.curve_c;
        o.slip_drive = a.slip_drive, o.slip_brake = a.slip_brake, o.brake_peak = a.brake_peak;
        o.curve_d = a.curve_d, o.load_curve = a.load_curve;
        o.direct_slip = kinds[r][0], o.direct_lateral = kinds[r][1], o.x1da = kinds[r][2];
    }
    for (size_t a = 0; a < 2; ++a) {
        for (size_t s = 0; s < 16; ++s) x.tire_index[a][s] = p.tire_index[a][s];
        x.width[a] = p.width[a], x.camber_grip[a] = p.camber_grip[a], x.axle_grip[a] = p.axle_grip[a];
    }
    x.direct_lateral_scale = kHalfPi / speed_of(t.direct_speed[0]);
    return x;
}

void slip_ratios(ex::Car& c, const rules::TireExtra& x, const rules::WheelLayout& l, const Tables& t) {
    for (int i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        const rules::Tire& r = rules::tire_record(x, l, i, w.surface);
        const F a = w.steer;
        const F along = gt3r::ee::cos(a) * w.hub_vx - gt3r::ee::sin(a) * w.hub_vy;
        if (!r.direct_slip) {
            rules::slip_ratio(c, x, i, r, along, t.slip);
            continue;
        }
        ex::Drive& d = c.t[size_t(i)];
        const F rim = w.rim_speed;
        F s = along - rim;
        if (F() <= rim) {
            const gt3r::s8 v = along <= rim ? 1 : -1;
            d.slip_state = v, d.slip_sign = v;
        } else {
            s = -s;
            if (rim <= along) d.slip_sign = -1, d.slip_state = 1;
            else d.slip_sign = 1, d.slip_state = -1;
        }
        // The speed by the state: driving +0xE6 (then +0xE5), braking +0xE5 (then +0xE6), then 50 km/h.
        const u8 first = d.slip_state == 1 ? t.direct_speed[2] : t.direct_speed[1];
        const u8 second = d.slip_state == 1 ? t.direct_speed[1] : t.direct_speed[2];
        s = s / speed_of(first ? first : second);
        w.slip_gain = kOne;
        if (kOne < s) s = kOne;
        else if (s < kMinusOne) s = kMinusOne;
        w.slip_ratio = s;
    }
}

void convert(ex::Params& p, const Spec& spec) {
    const TireKinds kinds = tire_kinds(spec);
    for (size_t r = 0; r < 6; ++r) {
        ex::TireRecord& o = p.tire[r];
        if (kinds[r][1]) {
            F best = F(), at = F();
            for (int k = 0; k < o.curve_a.n && k < 8; ++k)
                if (best < o.curve_a.y[size_t(k)]) best = o.curve_a.y[size_t(k)], at = o.curve_a.x[size_t(k)];
            o.peak_slip = at;
        }
        if (o.grip == F()) continue;
        const size_t a = r / 3;
        const F unsprung = (gt3r::ee::int_to_f(spec.b((a ? 0x70u : 0x58u) + 3)) * F::raw(0x3E800000)) * F::raw(0x411CCCCC);
        const F load = (a ? p.xe58 : p.xe54) * F::raw(0x3F000000) + unsprung;
        o.grip = o.grip * rules::table(o.load_curve, load / p.width[a]);
    }
}

TireKinds tire_kinds(const Spec& spec) {
    TireKinds k{};
    for (int r = 0; r < 6; ++r)
        for (u32 j = 0; j < 3; ++j) k[size_t(r)][j] = spec.b(Spec::tire_block(r) + 3 + j);
    return k;
}

CarPart car_part(const ex::Car& c, const Spec& spec, const Tables& t) {
    CarPart x;
    x.layout = wheel_layout(t, spec.b(0x02)); // (Gran Turismo 4's spec +0x02, as rules_build's wheel_layout_index)
    x.tires = tire_extra(c.p, tire_kinds(spec), t);
    x.chassis.mass = c.p.mass;
    x.axle_load = {c.p.xe54, c.p.xe58};
    return x;
}

void wheel_kinds(const ex::Car& c, CarPart& x) {
    for (int w = 0; w < 4; ++w) x.tires.wheel_xdd[size_t(w)] = rules::tire_record(x.tires, x.layout, w, c.w[size_t(w)].surface).x1da;
}

void lateral_yaw(ex::Car& c, const CarPart& x, bool air, const Tables& t) {
    using gt3r::ee::cos;
    using gt3r::ee::sin;
    const rules::WheelLayout& l = x.layout;
    const rules::TireExtra& tx = x.tires;
    const int n = l.count < 4 ? l.count : 4;
    if (air) {
        c.lat_g = F(), c.a_y = F();
        return;
    }
    F lat = F(), torque = F();
    for (int i = 0; i < n; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        const F steer = w.steer;
        if (t.globals.steer_mode == 1) w.steer = w.steer + tx.steer_offset[size_t(i)];
        else if (t.globals.steer_mode == 2) w.steer = tx.steer_offset[size_t(i)];
        const F gx = w.fx_drive + w.fx_brake;
        F fa, fb;
        if (rules::tire_record(tx, l, i, w.surface).x1da == 1) {
            fa = -cos(w.steer) * w.force_a;
            fb = -sin(w.steer) * w.force_a;
        } else {
            const F cs = cos(w.slip_dir), sn = sin(w.slip_dir);
            fa = -cs * w.force_a + sn * w.force_b;
            fb = -sn * w.force_a + -cs * w.force_b;
        }
        const F cam = tx.camber_force[size_t(i)];
        fa = fa + -cos(w.steer) * cam;
        fb = fb + -sin(w.steer) * cam;
        const F hold = ex::low_speed_hold(w, true);
        fa = fa - cos(w.steer) * hold;
        fb = fb - sin(w.steer) * hold;
        fa = fa - sin(w.steer) * gx;
        fb = fb + cos(w.steer) * gx;
        lat = lat + fa;
        const u8 k = l.geometry[size_t(i)] & 1u;
        const u8 side = l.side[size_t(i)];
        const F lx = side == 0 ? -c.p.half_track[k] : side == 1 ? c.p.half_track[k] : F();
        const F dist = k ? c.p.dist_rear : c.p.dist_front;
        const gt3r::s8 fa_pos = gt3r::s8(l.fore_aft[size_t(i)]);
        const F ly = fa_pos > 0 ? dist : fa_pos < 0 ? -dist : F();
        torque = torque + (lx * fb - ly * fa);
        w.steer = steer;
    }
    c.yaw_accel = torque * c.p.inv_yaw_inertia;
    const F r = c.yaw_rate + c.yaw_accel * c.dt;
    const F hi = F::raw(0x40C90FDA), lo = F::raw(0xC0C90FDA);
    c.yaw_rate = hi < r ? hi : r < lo ? lo : r;
    c.a_y = lat * c.p.inv_mass;
    F grip = F();
    for (int i = 0; i < n; ++i) {
        const u8 s = c.w[size_t(i)].surface;
        const rules::Tire& r2 = rules::tire_record(tx, l, i, s);
        grip = grip + r2.mu[s % 24] * r2.grip;
    }
    c.lat_g = c.a_y * gt3r::ee::int_to_f(n) / grip;
}

void dynamics(ex::Car& c, CarPart& x, const ex::Statics& s, const ex::GearSettings& g, const ex::ClassGrip& grip, const Tables& t) {
    ex::brakes_and_clutch(c);
    ex::rolling_resistance(c, s);
    ex::tyre_curves(c);
    ex::class_grip_scale(c, grip);
    ex::tyre_long_force(c, s);
    rules::tire_slip_force(c, x.tires, x.layout, t.globals);
    if (t.globals.mean_on) c.x420 = x.tires.mean_slip;
    wheel_kinds(c, x);
    rules::forward_step(c, x.chassis, x.tires, x.layout);
    ex::wheel_net_torque(c);
    ex::torque_vectoring(c, s.hybrid);
    ex::axle_step(c, s.awd3, s.stepless, s.hybrid);
    ex::wheel_speeds(c);
    ex::engine_speed(c, s.hybrid);
    ex::gear_step(c, g);
    const bool air = ex::airborne(c);
    lateral_yaw(c, x, air, t);
    ex::stop_hold(c, air);
}

namespace {

const F kHalf = F::raw(0x3F000000), kPi = F::raw(0x40490FDB);

// The aim of wheel w's steering offset.
F offset_aim(const ex::Car& c, const CarPart& x, const rules::SteerTables& g, int w) {
    const ex::Wheel& wh = c.w[size_t(w)];
    const bool fixed = wh.surface < g.fixed_aim.size() && g.fixed_aim[wh.surface] == 1;
    if (g.mode == 2) {
        if (fixed) return wh.steer;
        if (wh.slip_fade == F()) return x.tires.steer_offset[size_t(w)];
        if (wh.hub_vx < F()) return rules::angle_blend(kHalf, wh.slip_dir - kPi, kHalf, wh.steer);
        return (wh.slip_dir + wh.steer) * kHalf;
    }
    if (fixed || wh.slip_fade == F()) return F();
    if (wh.hub_vx < F()) return rules::angle_diff(wh.slip_dir - kPi, wh.steer) * kHalf;
    return (wh.slip_dir - wh.steer) * kHalf;
}

} // namespace

void wheel_steer(ex::Car& c, CarPart& x, const Tables& t) {
    ex::wheel_steer(c);
    const rules::SteerTables& g = t.steer;
    if (g.mode == 0) return;
    const rules::WheelLayout& l = x.layout;
    for (int w = 0; w < l.count && w < 4; ++w) {
        const size_t i = size_t(w), k = l.geometry[i] & 1u;
        const ex::Wheel& wh = c.w[i];
        const F aim = offset_aim(c, x, g, w);
        const F off = x.tires.steer_offset[i], rate = x.offset_rate[i];
        F push = g.spring[k] * (aim - off) - g.damping[k] * rate;
        F lim = g.limit[k];
        if (g.scale != 0) {
            const F share = (wh.peak_force * gt3r::ee::int_to_f(l.group_wheels[k])) / x.axle_load[k];
            if (g.scale == 2 || g.scale == 3) push = push * share;
            if (g.scale == 1 || g.scale == 3) lim = lim * share;
        }
        x.offset_rate[i] = rate + push * c.dt;
        x.tires.steer_offset[i] = off + x.offset_rate[i] * c.dt;
        const F o = x.tires.steer_offset[i];
        if (g.mode == 2) {
            const F d = o - wh.steer;
            if (lim < d) x.tires.steer_offset[i] = wh.steer + lim, x.offset_rate[i] = F();
            else if (d < -lim) x.tires.steer_offset[i] = wh.steer - lim, x.offset_rate[i] = F();
        } else {
            if (lim < o) x.tires.steer_offset[i] = lim, x.offset_rate[i] = F();
            else if (o < -lim) x.tires.steer_offset[i] = -lim, x.offset_rate[i] = F();
        }
    }
}

void ground_frame(ex::Car& c) {
    ex::ground_frame(c);
    rules::body_axes(c.ground_pitch, c.ground_roll, c.heading, c.ground_fwd, c.ground_lat, c.ground_up);
}

} // namespace gt4r::sim::prologue
