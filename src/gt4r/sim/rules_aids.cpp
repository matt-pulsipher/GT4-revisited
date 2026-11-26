// Gran Turismo 4's own routines of the race step: the driving aids (rules_aids.h).

#include "gt4r/sim/rules_aids.h"

#include "base/ee_math.h"
#include "gt4r/sim/program.h"

namespace gt4r::sim::rules {

namespace {

using gt3r::ee::int_to_f;

const F k0 = F::raw(0), kHalf = F::raw(0x3F000000), kOne = F::raw(0x3F800000);
const F kTenth = F::raw(0x3DCCCCCC);       // 0.1, a unit low
const F kHundredth = F::raw(0x3C23D70A);   // 0.01
const F kFifth = F::raw(0x3E4CCCCC);       // 0.2, a unit low
const F kDeg = F::raw(0x3C8EFA34);         // pi / 180, a unit low
const F kQuarterTurn = F::raw(0x3FC90FD9); // pi / 2, two units low
const F kOverSpeed = F::raw(0x3C9374BD);   // 0.018 per m/s
const F kSlowest = F::raw(0x40DE38E2);     // 25 km/h
const F kPerKmh = F::raw(0x3E8E38E3);      // 1 / 3.6
const F kHalfKmh = F::raw(0x3E0E38E3);     // 0.5 km/h
const F kFiveKmh = F::raw(0x3FB1C71B);     // 5 km/h
const F kTenKmh = F::raw(0x4031C71B);      // 10 km/h
const F kHundredKmh = F::raw(0x41DE38E2);  // 100 km/h
const F kFollowSpring = F::raw(0x43FA0000), kFollowDamp = F::raw(0x41A00000); // 500, 20
const F kTcsDamp = F::raw(0x42480000);     // 50
const F kTcsLead = F::raw(0x3F8CCCCC);     // 1.1
const F kTick = F::raw(0x3C888888);        // 1 / 60
const F kHold = F::raw(0x3FC00000), kHoldStart = F::raw(0x3E199999); // 1.5, 0.15
const F kHundred = F::raw(0x42C80000);
const F kFloor = F::raw(0x3A83126E);       // 0.001, a unit low

int count(const WheelLayout& l) { return l.count < 4 ? l.count : 4; }
size_t axle_of(const WheelLayout& l, int w) { return w < l.count ? l.axle[size_t(w)] : 0; }
s8 fore_aft(const WheelLayout& l, int w) { return w < l.count ? s8(l.fore_aft[size_t(w)]) : s8(0); }
F abs_of(F v) { return v < k0 ? -v : v; }
F clamp01(F v) {
    if (kOne < v) return kOne;
    if (v < k0) return k0;
    return v;
}

// 0x3F9CF0: the TCS's follower set to the throttle, at rest.
void settle(const ex::Car& c, AidExtra& x) {
    x.tcs_throttle = c.throttle;
    x.tcs_throttle_v = k0;
}

// 0x35FC68: the start counter's value, then one step off it while it runs.
u16 start_counter(AidExtra& x) {
    const u16 n = x.start_count;
    x.start_count = u16(n - (n != 0 ? 1 : 0));
    return n;
}

// 0x354E68: a car with a tank and none left in it.
bool out_of_fuel(const AidExtra& x) { return k0 < x.fuel_capacity && x.fuel <= k0; }

// The lower of a set cap and another (0: not set).
void lower(u32& cap, u8 other) {
    if (cap == 0 || other < cap) cap = other;
}

} // namespace

std::array<F, 5> tcs_classes(const s8* bytes) {
    std::array<F, 5> f;
    for (size_t k = 0; k < 5; ++k) f[k] = int_to_f(s32(bytes[k]) + 100) / kHundred;
    return f;
}

AidTables aid_tables(const Program& program) {
    AidTables g = aid_tables([&](u32 a) { return program.w(a); }, [&](u32 a) { return program.b(a); });
    s8 b[5];
    for (u32 k = 0; k < 5; ++k) b[k] = s8(program.b(kTcsClassBytes + k));
    g.tcs_class = tcs_classes(b);
    g.japan = program.release() == Release::Japan;
    return g;
}

u8 block_tcs_on(const ex::Aids& a) { return a.tcs_gain == k0 ? 0 : 1; }

u8 block_ascc_on(const ex::Aids& a, const AidTables& g) {
    if ((a.on[0] | a.on[1]) != 0) return 1;
    if (g.c_status == 0) return 0;
    if (a.on[2] != 0) return 1;
    if (g.e_quiet != 0) return 0;
    return a.on[3] != 0 ? 1 : 0;
}

size_t tire_index(const TireExtra& t, const WheelLayout& l, int w, u8 surface) {
    return t.tire_index[axle_of(l, w) & 1][surface < 24 ? surface : 0];
}

F rising_slip(s16 n, const F* x, const F* y, F v) {
    if (v <= y[0]) return x[0];
    F f = x[0];
    for (int i = 0; i < n - 1; ++i) {
        if (k0 <= x[i]) return k0;
        if (!(y[i + 1] < v)) {
            const F t = (v - y[i]) / (y[i + 1] - y[i]);
            return x[i + 1] * t + x[i] * (kOne - t);
        }
        f = x[i + 1];
    }
    return f;
}

F falling_slip(s16 n, const F* x, const F* y, F v) {
    if (v <= y[n - 1]) return x[n - 1];
    F f = x[n - 1];
    for (int i = n - 1; i > 0; --i) {
        if (x[i] <= k0) return k0;
        if (!(y[i - 1] < v)) {
            const F t = (v - y[i]) / (y[i - 1] - y[i]);
            return x[i - 1] * t + x[i] * (kOne - t);
        }
        f = x[i - 1];
    }
    return f;
}

void indicators(ex::Car& c, AidExtra& x, const TireExtra& t, const WheelLayout& l, const AidTables& g) {
    F under = k0, over = k0;
    if (!(c.speed <= k0)) {
        F grip = k0;
        for (int w = 0; w < count(l); ++w) grip = grip + tire_record(t, l, w, c.w[size_t(w)].surface).grip;
        F steer = c.steer_cmd, r = c.yaw_rate, ra = c.yaw_accel;
        if (k0 <= steer * r) {
            if (steer < k0) steer = -steer, r = -r, ra = -ra;
            const F rt = c.speed * gt3r::ee::sin(steer) / c.p.wheelbase;
            if (k0 < rt && kFifth < abs_of(c.a_y) / grip) {
                if (k0 < ra * r) r = r + ra * kHalf;
                if (r < rt) under = kOne - r / rt;
            }
        }
        const u8 control = u8(c.control);
        const F v = c.speed;
        if (control != 1 && control != 2 && c.driver_kind == 0) {
            if (x.scale_on != 0 && g.under_scale_on != 0) {
                under = under * g.under_scale;
                if (kOne < under) under = kOne;
            }
            // The largest slip angle of the wheels behind the center of gravity.
            for (int w = 0; w < count(l); ++w) {
                if (fore_aft(l, w) >= 0) continue;
                const F b = abs_of(c.t[size_t(w)].slip_angle);
                if (over < b) over = b;
            }
            F q = gt3r::ee::sqrt(v * v + c.lat_speed * c.lat_speed) * kOverSpeed;
            if (kOne < q) q = kOne;
            const F a0 = int_to_f(g.over_angle[0]) * kDeg, a1 = int_to_f(g.over_angle[1]) * kDeg;
            const F threshold = q * (a1 - a0) + a0;
            if (threshold <= over) {
                const F past = over - threshold;
                const F g0 = int_to_f(g.over_gain[0]) * kTenth, g1 = int_to_f(g.over_gain[1]) * kTenth;
                over = (q * (g1 - g0) + g0) * past / (kQuarterTurn - threshold);
                if (x.scale_on != 0 && g.over_scale_on != 0) over = over * g.over_scale;
                if (kOne < over) over = kOne;
            }
        } else {
            const F s = gt3r::ee::sqrt(v * v + c.lat_speed * c.lat_speed);
            if (!(s < kSlowest)) {
                F a = s * c.yaw_rate, lat = c.a_y;
                if (!(k0 <= a * lat)) {
                    a = abs_of(a) - abs_of(lat);
                    if (a < k0) a = k0;
                    over = (a + a) / grip;
                }
            }
        }
    }
    const F dt = c.dt;
    if (g.under_follow != 0) {
        const F rate = x.understeer_v + ((under - x.understeer) * kFollowSpring - x.understeer_v * kFollowDamp) * dt;
        x.understeer_v = rate;
        under = x.understeer + rate * dt;
    }
    if (g.over_follow != 0) {
        const F rate = x.oversteer_v + ((over - x.oversteer) * kFollowSpring - x.oversteer_v * kFollowDamp) * dt;
        x.oversteer_v = rate;
        over = x.oversteer + rate * dt;
    }
    x.oversteer = over;
    x.understeer = under;
}

F hold_throttle(const ex::Car& c, const AidExtra& x, const DriveExtra& d, F target) {
    target = target + kHalfKmh;
    const F v = c.gear == 0 ? -c.speed : c.speed;
    const F full = c.torque_wot + c.torque_friction;
    if (full <= k0) return kOne;
    const F past = (v - target) / kFiveKmh;
    const F gs = c.gear < d.d.gear_speed.size() ? d.d.gear_speed[c.gear] : k0;
    return clamp01((x.road_load + c.torque_friction * gs) / (full * gs) - past);
}

int start_pace(const ex::Car& c, const AidRace& r, u32& speed) {
    if (c.laps > 0 || c.driver_kind != 1 || r.start_speed == 0) return 0;
    speed = r.start_speed;
    if (int_to_f(s32(3 * r.start_speed)) * kPerKmh < r.lap_length - c.f3e4) return 1;
    speed = 0;
    return 2;
}

BrakeShares brake_shares() {
    BrakeShares s;
    for (size_t k = 0; k < 2; ++k) s.axle_brake[k] = kOne, s.axle_cap[k] = kOne;
    for (size_t k = 0; k < 4; ++k) s.cap_mul[k] = kOne, s.brake_mul[k] = kOne, s.brake_add[k] = k0;
    return s;
}

void ascc(ex::Car& c, AidExtra& x, const ex::Aids& a, BrakeShares& s, bool enabled, const AidTables& g) {
    for (size_t k = 0; k < 2; ++k) s.axle_cap[k] = s.axle_cap[k] * x.slip_scale[k];
    if (!enabled) return;
    u32 acted = 0;
    const F u = x.understeer, o = x.oversteer;
    const size_t in = k0 < c.yaw_rate ? 0 : 1, out = 1 - in; // the inner and outer wheel's slot (+2: the rear's)
    auto ease = [&](size_t k, F f) {
        s.cap_mul[k] = s.cap_mul[k] * f;
        s.brake_mul[k] = s.brake_mul[k] * f;
    };
    if (c.brake_pedal == k0) {
        if (a.on[0] != 0) {
            const F e = (o - a.a_thr) * a.a_gain;
            if (k0 < e) {
                acted = 1;
                if (a.a_wheels[0] != 0) s.brake_add[out] = s.brake_add[out] + e;
                if (a.a_wheels[1] != 0) s.brake_add[2 + out] = s.brake_add[2 + out] + e;
            }
        }
        if ((g.c_throttle != 0 || c.throttle_pedal == k0) && a.on[2] != 0) {
            const F e = (u - a.c_thr) * a.c_gain;
            if (k0 < e) {
                if (g.c_status != 0) acted |= 2;
                if (a.c_wheels[0] != 0) s.brake_add[in] = s.brake_add[in] + e;
                if (a.c_wheels[1] != 0) s.brake_add[2 + in] = s.brake_add[2 + in] + e;
            }
        }
    } else {
        if (a.on[1] != 0) {
            F f = kOne - (o - kTenth) * a.d_gain;
            if (f < k0) f = k0;
            if (f < kOne) {
                acted = 1;
                if (a.d_wheels[0] != 0) ease(in, f);
                if (a.d_wheels[1] != 0) ease(2 + in, f);
            }
        }
        if (a.on[3] != 0) {
            F f = kOne - (u - kTenth) * a.e_gain;
            if (f < k0) f = k0;
            if (f < kOne) {
                if (g.c_status != 0 && g.e_quiet == 0) acted |= 2;
                if (a.e_wheels[0] != 0) ease(out, f);
                if (a.e_wheels[1] != 0) ease(2 + out, f);
            }
        }
    }
    const u8 control = u8(c.control);
    if ((acted & 2) != 0 && g.ascc_cut != 0 && control != 1 && control != 2 && c.driver_kind == 0) {
        F f = kOne - u * g.ascc_cut_gain;
        if (f < k0) f = k0;
        c.throttle = c.throttle * f;
    }
    if (acted != 0) x.ascc_status = 2;
    else if (block_ascc_on(a, g)) x.ascc_status = 1;
}

void apply_shares(ex::Car& c, const WheelLayout& l, const BrakeShares& s, std::array<F, 4>& cap) {
    for (int w = 0; w < count(l); ++w) {
        const u8 axle = l.axle[size_t(w)];
        if (axle >= 2) continue;
        ex::Wheel& wh = c.w[size_t(w)];
        wh.brake = wh.brake * s.axle_brake[axle];
        cap[size_t(w)] = cap[size_t(w)] * s.axle_cap[axle];
        const u8 side = l.side[size_t(w)];
        if (side >= 2) continue;
        const size_t k = size_t(axle) * 2 + side;
        cap[size_t(w)] = cap[size_t(w)] * s.cap_mul[k];
        const F b = wh.brake * s.brake_mul[k] + s.brake_add[k];
        wh.brake = kOne < b ? kOne : b;
    }
}

void brakes(ex::Car& c, AidExtra& x, const TireExtra& t, const WheelLayout& l, const AidTables& g, const AidRace& r, const ex::Aids& a,
            bool ascc_enabled) {
    BrakeShares s = brake_shares();
    std::array<F, 4> cap{};
    for (int w = 0; w < count(l); ++w) cap[size_t(w)] = tire_record(t, l, w, c.w[size_t(w)].surface).slip_brake;
    for (int w = 0; w < count(l); ++w) {
        const u8 blend = g.cap_blend[l.axle[size_t(w)] & 1];
        if (blend == 0) continue;
        const size_t idx = tire_index(t, l, w, c.w[size_t(w)].surface);
        const Tire& rec = t.tire[idx];
        if (rec.direct_slip != 1) continue;
        const F slip = falling_slip(rec.curve_c.n, rec.curve_c.x.data(), rec.curve_d.data(), t.curve_a[size_t(w)]);
        if (slip < cap[size_t(w)]) {
            const F k = int_to_f(blend) / kHundred;
            cap[size_t(w)] = k * slip + (kOne - k) * cap[size_t(w)];
        }
    }
    if (r.course_kind != 0 && g.dirt_steer != 0) {
        const F full = int_to_f(g.dirt_steer) * kDeg, steer = abs_of(c.steer_cmd);
        const F u = full <= steer ? kOne : steer / full;
        for (size_t k = 0; k < 2; ++k) s.axle_brake[k] = s.axle_brake[k] * (kOne - u * (int_to_f(g.dirt_cut[k]) * kHundredth));
    }
    ascc(c, x, a, s, ascc_enabled, g);
    if (x.mark_mode == 2)
        for (size_t k = 0; k < 4; ++k) x.ascc_mark[k] = s.cap_mul[k] == kOne && s.brake_mul[k] == kOne && s.brake_add[k] == k0 ? 0 : 1;
    apply_shares(c, l, s, cap);
    // The ABS.
    for (int w = 0; w < count(l); ++w) {
        ex::Wheel& wh = c.w[size_t(w)];
        const size_t axle = axle_of(l, w) & 1;
        const F p = wh.brake, peak = wh.peak_force * x.abs_peak[axle];
        if (p == k0) continue;
        F f = peak * (kOne - (wh.slip_ratio - cap[size_t(w)]) * (x.abs_gain[axle] * wh.slip_gain));
        if (f < k0) f = k0;
        else if (kOne < f) f = kOne;
        f = f * p;
        wh.brake = f == k0 ? kFloor : f;
    }
}

void tcs(ex::Car& c, AidExtra& x, const TireExtra& t, const DriveExtra& d, const WheelLayout& l, const AidTables& g, const ex::Aids& a) {
    const u8 on = block_tcs_on(a);
    x.tcs_status = on;
    if (!on) return settle(c, x);
    F lat = kOne;
    if (g.tcs_mode == 0) lat = ex::tcs_lateral(c.lat_g, a.tcs_lat_a, a.tcs_lat_b);
    if (c.torque_wot <= k0) return;
    if (c.clutch == 0) return settle(c, x);
    if (!(k0 < c.throttle)) return;
    // The most negative slip ratio of the loaded driven wheels, its tire's target and its gain.
    F slip = kOne, target = kOne, gain = kOne;
    const u8 layout = d.d.layout;
    for (int w = 0; w < count(l); ++w) {
        const s8 fa = fore_aft(l, w);
        const bool driven = layout == 1 ? fa > 0 : (layout == 0 || (layout >= 5 && layout < 8)) ? fa < 0 : true;
        if (!driven) continue;
        const ex::Wheel& wh = c.w[size_t(w)];
        if (!(k0 < wh.load) || !(wh.slip_ratio < slip)) continue;
        slip = wh.slip_ratio;
        const size_t idx = tire_index(t, l, w, wh.surface);
        const Tire& rec = t.tire[idx];
        gain = wh.slip_gain;
        target = rec.slip_drive;
        if (g.tcs_mode == 1) {
            const F s = rising_slip(rec.curve_c.n, rec.curve_c.x.data(), rec.curve_d.data(), t.curve_a[size_t(w)]);
            if (target < s) target = s;
        }
    }
    if (g.tcs_mode == 0) target = target * lat;
    target = target * a.tcs_scale;
    if (c.control == 1 && c.driver_kind == 0) target = target * (x.tcs_class < g.tcs_class.size() ? g.tcs_class[x.tcs_class] : kOne);
    const F v = c.speed;
    if (g.tcs_speed_on != 0) {
        const F lo = int_to_f(g.tcs_speed_lo) * kTenKmh;
        if (lo < v) {
            const F hi = int_to_f(g.tcs_speed_hi) * kTenKmh;
            F k = int_to_f(g.tcs_speed_gain) * kHundredth + kOne;
            if (v < hi) k = k * ((v - lo) / (hi - lo));
            target = target * k;
        }
    }
    F f = (slip - target) * (a.tcs_gain * gain) + kOne;
    if (f < k0) f = k0;
    else if (kOne < f) f = kOne;
    if (v < kHundredKmh) {
        if (f < kOne) x.tcs_status = 2;
        c.throttle = c.throttle * f;
        if (!g.japan) settle(c, x);
        return;
    }
    // At speed the throttle follows a damped target a tenth above the limited throttle.
    const F rate = x.tcs_throttle_v + ((c.throttle * f * kTcsLead - x.tcs_throttle) * kFollowSpring - x.tcs_throttle_v * kTcsDamp) * kTick;
    x.tcs_throttle_v = rate;
    x.tcs_throttle = x.tcs_throttle + rate * kTick;
    if (c.throttle < x.tcs_throttle) x.tcs_throttle = c.throttle, x.tcs_throttle_v = k0;
    else if (x.tcs_throttle < k0) x.tcs_throttle = k0, x.tcs_throttle_v = k0;
    if (x.tcs_throttle < c.throttle) {
        x.tcs_status = 2;
        c.throttle = x.tcs_throttle;
    }
}

void own_aids(ex::Car& c, AidExtra& x, const TireExtra& t, const DriveExtra& d, const WheelLayout& l, const AidTables& g, const AidRace& r) {
    const ex::Aids a = x.own;
    tcs(c, x, t, d, l, g, a);
    brakes(c, x, t, l, g, r, a, c.handbrake < kTenth);
}

void driver_aids(ex::Car& c, AidExtra& x, const TireExtra& t, const DriveExtra& d, const WheelLayout& l, const AidTables& g, const AidRace& r,
                 const ex::Aids& a) {
    tcs(c, x, t, d, l, g, a);
    brakes(c, x, t, l, g, r, a, c.clutch != 2);
}

void aids(ex::Car& c, AidExtra& x, const TireExtra& t, const DriveExtra& d, const WheelLayout& l, const AidTables& g, const AidRace& r,
          const ex::Aids* driver) {
    static const ex::Aids none{};
    const ex::Aids& block = driver ? *driver : none;
    indicators(c, x, t, l, g);
    x.tcs_status = 0, x.ascc_status = 0;
    const s8 control = s8(c.control);
    if (control > 0 && control < 3) {
        driver_aids(c, x, t, d, l, g, r, block);
        x.tcs_status = 0, x.ascc_status = 0;
    } else if (control == 0) {
        if (c.driver_kind == 0 || c.hold != 0) own_aids(c, x, t, d, l, g, r);
        else {
            driver_aids(c, x, t, d, l, g, r, block);
            x.tcs_status = block_tcs_on(x.own), x.ascc_status = block_ascc_on(x.own, g);
        }
    }
    // The race's speed caps.
    u32 cap = control == 2 ? r.speed_cap[1] : r.speed_cap[0];
    if (start_counter(x) != 0) lower(cap, r.start_cap);
    if (out_of_fuel(x)) lower(cap, r.fuel_cap);
    if (x.pit_limit != 0) cap = 10;
    u32 pace = 0;
    if (start_pace(c, r, pace) == 1) cap = pace;
    if (cap != 0) {
        const F f = hold_throttle(c, x, d, int_to_f(s32(cap)) * kPerKmh);
        if (f < c.throttle) c.throttle = f;
    }
    // The cornering throttle hold.
    if ((x.assist & 2) != 0 && (control == 0 || control == 3) && c.driver_kind == 0) {
        F k = c.lat_g * c.lat_g * kHold - kHoldStart;
        if (kOne < k) k = kOne;
        else if (k < k0) k = k0;
        const F f = hold_throttle(c, x, d, c.speed) * k;
        if (c.throttle_pedal < f) c.throttle = f;
    }
    c.understeer = x.understeer, c.oversteer = x.oversteer;
    c.tcs_status = x.tcs_status, c.ascc_status = x.ascc_status;
}

ex::Aids aid_block(const AidBytes& b) {
    ex::Aids a;
    const u8 bits = b.wheels;
    a.a_gain = int_to_f(b.a_gain) * kHundredth, a.a_thr = int_to_f(b.a_thr) * kHundredth, a.d_gain = int_to_f(b.d_gain) * kHundredth;
    a.a_wheels = {u8(bits & 1), u8((bits >> 1) & 1)}, a.d_wheels = {u8((bits >> 2) & 1), u8((bits >> 3) & 1)};
    a.on[0] = (a.a_wheels[0] | a.a_wheels[1]) != 0 && !(a.a_gain == k0) ? 1 : 0;
    a.on[1] = (a.d_wheels[0] | a.d_wheels[1]) != 0 && !(a.d_gain == k0) ? 1 : 0;
    a.c_gain = int_to_f(b.c_gain) * kHundredth, a.c_thr = int_to_f(b.c_thr) * kHundredth, a.e_gain = int_to_f(b.e_gain) * kHundredth;
    a.c_wheels = {u8((bits >> 4) & 1), u8((bits >> 5) & 1)}, a.e_wheels = {u8((bits >> 6) & 1), u8((bits >> 7) & 1)};
    a.on[2] = (a.c_wheels[0] | a.c_wheels[1]) != 0 && !(a.c_gain == k0) ? 1 : 0;
    a.on[3] = (a.e_wheels[0] | a.e_wheels[1]) != 0 && !(a.e_gain == k0) ? 1 : 0;
    a.tcs_lat_a = int_to_f(b.tcs_lat_a) * kTenth, a.tcs_lat_b = int_to_f(b.tcs_lat_b) * kHundredth, a.tcs_gain = int_to_f(b.tcs_gain) * kTenth;
    a.tcs_scale = b.tcs_scale == 100 || b.tcs_scale == 0 ? kOne : int_to_f(b.tcs_scale) * kHundredth;
    return a;
}

ex::Aids driver_block(const u8* s) { return aid_block({s[9], s[10], s[20], s[19], s[21], s[22], s[23], s[11], s[12], s[13], s[24]}); }

void aid_extras(const u8* spec, const std::array<F, 2>& brake_torque, const std::array<F, 2>& tire_radius, AidExtra& x) {
    x.own = aid_block({spec[0x1BA], spec[0x1BB], spec[0x1BC], spec[0x1B8], spec[0x1BD], spec[0x1BE], spec[0x1BF], spec[0x1C0], spec[0x1C1], spec[0x1C2],
                       spec[0x1C3]});
    for (size_t k = 0; k < 2; ++k) {
        x.abs_gain[k] = int_to_f(spec[0x1A8 + k]) * kTenth;
        x.slip_scale[k] = int_to_f(spec[0x1AA + k]) * kHundredth;
        x.abs_peak[k] = spec[0x178 + k] == 0 ? k0 : tire_radius[k] / brake_torque[k];
    }
    x.mark_mode = spec[0x01];
}

} // namespace gt4r::sim::rules
