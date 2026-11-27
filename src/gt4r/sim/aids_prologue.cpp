// Gran Turismo 4 Prologue's driving aids (aids_prologue.h). Addresses are SCPS-15055 v1.01's.

#include "gt4r/sim/aids_prologue.h"

#include "base/ee_math.h"

#include "gt4r/sim/program.h"
#include "gt4r/sim/program_prologue.h"
#include "gt4r/sim/rules_aids.h"

namespace gt4r::sim::prologue {

namespace {
using gt3r::ee::int_to_f;
constexpr F k0 = F::raw(0), kOne = F::raw(0x3F800000), kHundred = F::raw(0x42C80000), kHundredth = F::raw(0x3C23D70A);
constexpr F kByte = F::raw(0x3C000000);  // 1 / 128
constexpr F kDeg = F::raw(0x3C8EFA34);
constexpr F kTenth = F::raw(0x3DCCCCCC);
constexpr F kFloor = F::raw(0x3A83126E); // 0.001
constexpr F kKmh = F::raw(0x3E8E38E3);
F abs_of(F v) { return v < k0 ? -v : v; }
int count(const rules::WheelLayout& l) { return l.count < 4 ? l.count : 4; }
// The wheel's curve A byte as 0x3BB138 stores it (its value x 128, truncated, a byte) and as the aids read it.
F curve_a_of(const CarPart& x, int w) {
    const gt3r::s32 b = gt3r::ee::to_int(x.tires.curve_a[size_t(w)] * F::raw(0x43000000));
    return int_to_f(u8(b)) * kByte;
}
} // namespace

u8 ascc_on(const AidTables& g, const ex::Aids& a) {
    if (a.on[0] != 0 || a.on[1] != 0) return 1;
    if (g.c_status == 0) return 0;
    if (a.on[2] != 0) return 1;
    if (g.e_quiet != 0) return 0;
    return a.on[3] != 0 ? 1 : 0;
}

F curve_a_byte(F curve_a) { return int_to_f(u8(gt3r::ee::to_int(curve_a * F::raw(0x43000000)))) * kByte; }

AidTables aid_tables(const Program& program) {
    AidTables g;
    const u32 s = PrologueProgram::kStatic;
    g.cap_blend = {program.b(s + 0xE1), program.b(s + 0xE2)};
    g.tcs_mode = program.b(s + 0xE3);
    g.dirt_steer = program.b(s + 0xB5);
    g.dirt_cut = {program.b(s + 0xB6), program.b(s + 0xB7)};
    g.c_status = program.b(PrologueProgram::kAsmKeep - 2), g.c_throttle = program.b(PrologueProgram::kAsmKeep - 1);
    g.e_quiet = program.b(PrologueProgram::kAsmKeep);
    g.over_angle = {program.b(s + 0xAD), program.b(s + 0xAE)}, g.over_gain = {program.b(s + 0xAF), program.b(s + 0xB0)};
    return g;
}

void steer_indicators(ex::Car& c, const CarPart& x, const AidTables& g) {
    const rules::WheelLayout& l = x.layout;
    const int n = count(l);
    c.understeer = k0, c.oversteer = k0;
    const F v = c.speed;
    if (v <= k0) return;
    F grip = k0;
    for (int w = 0; w < n; ++w) grip = grip + rules::tire_record(x.tires, l, w, c.w[size_t(w)].surface).grip;
    F delta = c.steer_cmd, r = c.yaw_rate, ra = c.yaw_accel;
    if (k0 <= delta * r) {
        if (delta < k0) delta = -delta, r = -r, ra = -ra;
        const F rt = v * gt3r::ee::sin(delta) / c.p.wheelbase;
        if (k0 < rt) {
            F lat = c.a_y;
            if (lat < k0) lat = -lat;
            lat = lat / grip;
            if (F::raw(0x3E4CCCCC) < lat) {
                if (k0 < ra * r) r = r + ra * F::raw(0x3F000000);
                if (r < rt) c.understeer = kOne - r / rt;
            }
        }
    }
    if (u8(c.control - 1) < 2 || c.driver_kind != 0) {
        const F sp = gt3r::ee::sqrt(v * v + c.lat_speed * c.lat_speed);
        if (sp < F::raw(0x40DE38E2)) return; // (25 km/h)
        F a = sp * c.yaw_rate, lat = c.a_y;
        if (k0 <= a * lat) return;
        if (a < k0) a = -a;
        if (lat < k0) lat = -lat;
        a = a - lat;
        if (a < k0) a = k0;
        c.oversteer = (a + a) / grip;
        return;
    }
    F m = k0;
    for (int w = 0; w < n; ++w) {
        if (!(gt3r::s8(l.fore_aft[size_t(w)]) < 0)) continue;
        F b = c.t[size_t(w)].slip_angle;
        if (b < k0) b = -b;
        if (m < b) m = b;
    }
    F q = gt3r::ee::sqrt(v * v + c.lat_speed * c.lat_speed) * F::raw(0x3C9374BD);
    if (kOne < q) q = kOne;
    const F deg = F::raw(0x3C8EFA34), tenth = F::raw(0x3DCCCCCC);
    const F a0 = gt3r::ee::int_to_f(g.over_angle[0]) * deg, a1 = gt3r::ee::int_to_f(g.over_angle[1]) * deg;
    const F th = q * (a1 - a0) + a0;
    if (!(th <= m)) return;
    m = m - th;
    const F g1 = gt3r::ee::int_to_f(g.over_gain[1]) * tenth, g0 = gt3r::ee::int_to_f(g.over_gain[0]) * tenth;
    const F span = F::raw(0x3FC90FD9) - th;
    F o = (q * (g1 - g0) + g0) * m / span;
    if (kOne < o) o = kOne;
    c.oversteer = o;
}

void tcs(ex::Car& c, const CarPart& x, const AidTables& g, const ex::Aids& a) {
    const u8 on = a.tcs_gain == k0 ? 0 : 1; // 0x3E1940
    c.tcs_status = on;
    if (!on) return;
    F lat = kOne;
    if (g.tcs_mode == 0) lat = rules::tcs_lateral(c.lat_g, a.tcs_lat_a, a.tcs_lat_b); // 0x3E18F0 (= 0x3F9C80)
    if (c.torque_wot <= k0) return;
    if (!(k0 < c.throttle)) return;
    // The most negative slip ratio of the loaded driven wheels (by the driveline's layout: 1 the wheels ahead of the
    // center of gravity, 0 and 5 .. 7 those behind it, others all), its tire's driving target and its slip gain.
    const rules::WheelLayout& l = x.layout;
    const u8 layout = c.p.d.layout;
    F slip = kOne, target = kOne, gain = kOne;
    for (int w = 0; w < count(l); ++w) {
        const gt3r::s8 fa = gt3r::s8(l.fore_aft[size_t(w)]);
        const bool driven = layout == 1 ? fa > 0 : (layout == 0 || (layout >= 5 && layout < 8)) ? fa < 0 : true;
        if (!driven) continue;
        const ex::Wheel& wh = c.w[size_t(w)];
        if (!(k0 < wh.load) || !(wh.slip_ratio < slip)) continue;
        slip = wh.slip_ratio;
        const rules::Tire& rec = rules::tire_record(x.tires, l, w, wh.surface);
        gain = wh.slip_gain;
        target = rec.slip_drive;
        if (g.tcs_mode == 1) {
            const F s = rules::rising_slip(rec.curve_c.n, rec.curve_c.x.data(), rec.curve_d.data(), curve_a_of(x, w)); // 0x3C1730
            if (target < s) target = s;
        }
    }
    if (g.tcs_mode == 0) target = target * lat;
    target = target * a.tcs_scale;
    F f = (slip - target) * (a.tcs_gain * gain) + kOne;
    if (f < k0) f = k0;
    else if (kOne < f) f = kOne;
    if (f < kOne) c.tcs_status = 2;
    c.throttle = c.throttle * f;
}

void ascc(ex::Car& c, const AidTables& g, const ex::Aids& a, rules::BrakeShares& s, bool enabled) {
    for (size_t k = 0; k < 2; ++k) s.axle_cap[k] = c.p.brake_slip_scale[k] * s.axle_cap[k];
    if (!enabled) return;
    bool acted = false;
    const F u = c.understeer, o = c.oversteer;
    const size_t in = k0 < c.yaw_rate ? 0 : 1, out = 1 - in; // the inner and outer wheel's slot (+2: the rear's)
    auto ease = [&](size_t k, F f) {
        s.cap_mul[k] = f * s.cap_mul[k];
        s.brake_mul[k] = f * s.brake_mul[k];
    };
    if (c.brake_pedal == k0) {
        if (a.on[0] != 0) {
            const F e = (o - a.a_thr) * a.a_gain;
            if (k0 < e) {
                acted = true;
                if (a.a_wheels[0] != 0) s.brake_add[out] = s.brake_add[out] + e;
                if (a.a_wheels[1] != 0) s.brake_add[2 + out] = s.brake_add[2 + out] + e;
            }
        }
        if ((g.c_throttle != 0 || c.throttle_pedal == k0) && a.on[2] != 0) {
            const F e = (u - a.c_thr) * a.c_gain;
            if (k0 < e) {
                if (g.c_status != 0) acted = true;
                if (a.c_wheels[0] != 0) s.brake_add[in] = s.brake_add[in] + e;
                if (a.c_wheels[1] != 0) s.brake_add[2 + in] = s.brake_add[2 + in] + e;
            }
        }
    } else {
        if (a.on[1] != 0) {
            F f = kOne - (o - kTenth) * a.d_gain;
            if (f < k0) f = k0;
            if (f < kOne) {
                acted = true;
                if (a.d_wheels[0] != 0) ease(in, f);
                if (a.d_wheels[1] != 0) ease(2 + in, f);
            }
        }
        if (a.on[3] != 0) {
            F f = kOne - (u - kTenth) * a.e_gain;
            if (f < k0) f = k0;
            if (f < kOne) {
                if (g.c_status != 0 && g.e_quiet == 0) acted = true;
                if (a.e_wheels[0] != 0) ease(out, f);
                if (a.e_wheels[1] != 0) ease(2 + out, f);
            }
        }
    }
    if (acted) c.ascc_status = 2;
    else if (ascc_on(g, a)) c.ascc_status = 1;
}

void brakes(ex::Car& c, const CarPart& x, const AidTables& g, const AidRace& r, const ex::Aids& a, bool ascc_enabled) {
    const rules::WheelLayout& l = x.layout;
    rules::BrakeShares s = rules::brake_shares(); // 0x3E1008
    std::array<F, 4> cap{};
    for (int w = 0; w < count(l); ++w) cap[size_t(w)] = rules::tire_record(x.tires, l, w, c.w[size_t(w)].surface).slip_brake;
    // Each wheel's ABS target blended toward curve D's falling side at its curve A value, for a tire whose slip ratio is
    // direct (the record's kind byte +0x1B4 1).
    for (int w = 0; w < count(l); ++w) {
        const u8 blend = g.cap_blend[l.axle[size_t(w)] & 1];
        if (blend == 0) continue;
        const rules::Tire& rec = rules::tire_record(x.tires, l, w, c.w[size_t(w)].surface);
        if (rec.direct_slip != 1) continue;
        const F slip = rules::falling_slip(rec.curve_c.n, rec.curve_c.x.data(), rec.curve_d.data(), curve_a_of(x, w)); // 0x3C17E8
        if (slip < cap[size_t(w)]) {
            const F k = int_to_f(blend) / kHundred;
            cap[size_t(w)] = k * slip + (kOne - k) * cap[size_t(w)];
        }
    }
    // The dirt cut: on a dirt course each axle's brake by the steering.
    if (r.dirt && g.dirt_steer != 0) {
        const F full = int_to_f(g.dirt_steer) * kDeg, steer = abs_of(c.steer_cmd);
        const F u = full <= steer ? kOne : steer / full;
        for (size_t k = 0; k < 2; ++k) s.axle_brake[k] = (kOne - u * (int_to_f(g.dirt_cut[k]) * kHundredth)) * s.axle_brake[k];
    }
    ascc(c, g, a, s, ascc_enabled);
    rules::apply_shares(c, l, s, cap); // 0x3E1078
    // The ABS: past the target the pressure falls with slope abs_gain x the slip gain, x the peak force's share; a pressure
    // that comes to 0 is kept at 0.001.
    for (int w = 0; w < count(l); ++w) {
        ex::Wheel& wh = c.w[size_t(w)];
        const size_t axle = l.axle[size_t(w)] & 1;
        const F p = wh.brake, peak = wh.peak_force * c.p.abs_peak_scale[axle];
        if (p == k0) continue;
        F f = peak * (kOne - (wh.slip_ratio - cap[size_t(w)]) * (c.p.abs_gain[axle] * wh.slip_gain));
        if (f < k0) f = k0;
        else if (kOne < f) f = kOne;
        f = f * p;
        wh.brake = f == k0 ? kFloor : f;
    }
}

void hold_speed(ex::Car& c, F target) {
    target = target + F::raw(0x3E0E38E3); // + 0.5 km/h
    F v = c.speed;
    if (c.gear == 0) v = -v;
    const F full = c.torque_wot + c.torque_friction;
    if (full <= k0) return;
    const F over = (v - target) / F::raw(0x3FB1C71B); // per 5 km/h
    const F slope = -((c.ground_fwd.z * c.p.mass) * F::raw(0x411CCCCC));
    const F gs = c.p.d.gear_speed[c.gear];
    F t = (-((c.rolling + c.drag) + slope) + c.torque_friction * gs) / (full * gs);
    t = t - over;
    if (kOne < t) t = kOne;
    else if (t < k0) t = k0;
    if (t < c.throttle) c.throttle = t;
}

void aids(ex::Car& c, const CarPart& x, const AidTables& g, const AidRace& r, const ex::AidInputs& in) {
    (void)in;
    steer_indicators(c, x, g); // 0x3E02A0
    c.tcs_status = 0, c.ascc_status = 0;
    const gt3r::s8 control = gt3r::s8(c.control);
    auto own = [&] { // 0x3E0778
        tcs(c, x, g, c.p.aids);
        brakes(c, x, g, r, c.p.aids, c.handbrake < F::raw(0x3DCCCCCC));
    };
    auto driver = [&] { // 0x3E07E8: the computer driver's block
        tcs(c, x, g, c.ai.aids);
        brakes(c, x, g, r, c.ai.aids, true);
    };
    auto statuses = [&] { // the car's own block's switches (0x3E1940, 0x3E11A8)
        c.tcs_status = c.p.aids.tcs_gain == k0 ? 0 : 1;
        c.ascc_status = ascc_on(g, c.p.aids);
    };
    if (control == 1) driver();
    else if (control == 0) {
        if (c.driver_kind == 0 || c.hold != 0) own();
        else driver(), statuses();
    } else if (control == 2) driver(), statuses();
    // The race's speed caps: the rule block's for the control source; a car of driver kind 5 at most 80 km/h; else,
    // while the car's penalty runs, the penalty's cap.
    u32 cap = control == 2 ? r.speed_cap[1] : r.speed_cap[0];
    if (c.driver_kind == 5) {
        if (cap == 0 || cap > 0x50) cap = 0x50;
    } else if (r.penalty && (cap == 0 || r.penalty_cap < cap)) cap = r.penalty_cap;
    if (cap != 0) hold_speed(c, int_to_f(gt3r::s32(cap)) * kKmh);
}

} // namespace gt4r::sim::prologue
