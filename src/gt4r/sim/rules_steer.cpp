// Gran Turismo 4's own routines of the race step: the wheels' steering (rules_steer.h).

#include "gt4r/sim/rules_steer.h"

#include "base/ee_math.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/program.h"
#include "sim/exact/build_math.h"

namespace gt4r::sim::rules {

namespace {

using gt3r::ee::int_to_f;

const F k0 = F::raw(0), kHalf = F::raw(0x3F000000), kThree = F::raw(0x40400000);
const F kTen = F::raw(0x41200000), kHundred = F::raw(0x42C80000), kThousand = F::raw(0x447A0000);
const F kHundredth = F::raw(0x3C23D70A);   // 0.01
const F kThousandth = F::raw(0x3A83126E);  // 0.001, a unit low
const F kDeg = F::raw(0x3C8EFA34);         // pi / 180, a unit low
const F kTenthDeg = F::raw(0x3AE4C385);    // pi / 1800
const F kPi = F::raw(0x40490FDA), kTwoPi = F::raw(0x40C90FDA), kMinusPi = F::raw(0xC0490FDA);
const F kGravity = F::raw(0x411CCCCC);     // 9.8, a unit low
const F kFollowGain = F::raw(0x41340000), kFollowSpring = F::raw(0x43480000), kFollowDamp = F::raw(0x41A00000); // 11.25, 200, 20
const F kTick = F::raw(0x3C888888);        // 1 / 60

constexpr gt3r::sim::Program kWrap = gt3r::sim::Program::ConceptGenevaAsia; // 0x36CB90: the 2002 Tokyo-Geneva turn constants

int count(const WheelLayout& l) { return l.count < 4 ? l.count : 4; }
s8 fore_aft(const WheelLayout& l, int w) { return w < l.count ? s8(l.fore_aft[size_t(w)]) : s8(0); }

// A side's ratio of the steering command: the wheel's own side when the command is >= 0, else the other.
F side_ratio(const ex::Car& c, u8 side, F delta) { return c.p.steer_ratio[size_t(k0 <= delta ? side : 1 - side)]; }

} // namespace

SteerTables steer_tables(const Program& program) {
    SteerTables g;
    if (!program.known()) return g;
    auto byte = [&](u32 a) { return program.b(a); };
    // 0x3F7BE0: the steering offset's settings.
    const u32 S = Program::kStatic;
    g.mode = byte(S + 0xED), g.scale = byte(S + 0xEE);
    for (u32 k = 0; k < 2; ++k) {
        const u8 lim = byte(S + 0xEF + k);
        if (lim == 0) g.mode = 0;
        g.limit[k] = int_to_f(lim) * kTenthDeg;
        g.spring[k] = int_to_f(byte(S + 0xF1 + k)) * kTen;
        g.damping[k] = int_to_f(byte(S + 0xF3 + k));
    }
    for (u32 s = 0; s < 24; ++s) g.fixed_aim[s] = byte(kFixedAim + s);
    // 0x366100 (its call's 4 and 2).
    const u32 A = kComplianceBytes;
    g.compliance_on = byte(A);
    g.compliance_max = (int_to_f(byte(A + 1)) * kHundredth) * kDeg;
    g.compliance_spring = int_to_f(byte(A + 2)) * kHundred;
    g.compliance_damping = int_to_f(byte(A + 3));
    const F two = int_to_f(2), six = int_to_f(4 + 2);
    g.age_curve.n = 2;
    g.age_curve.x = {int_to_f(gt3r::s16(program.h(A + 4))) * kThousand, int_to_f(gt3r::s16(program.h(A + 6))) * kThousand};
    g.age_curve.y = {k0, two};
    g.dead_band.n = 2, g.compliance_gain.n = 2;
    g.dead_band.x = g.compliance_gain.x = {k0, six};
    g.dead_band.y = {int_to_f(byte(A + 8)) / kHundred, int_to_f(byte(A + 9)) / kHundred};
    g.compliance_gain.y = {(int_to_f(byte(A + 0xA)) * kDeg) / kHundred, (int_to_f(byte(A + 0xB)) * kDeg) / kHundred};
    for (u32 k = 0; k < 3; ++k) g.lean_gain[k] = F::raw(program.w(kTwoWheelRows + kTwoWheelRowSize * k + 0x30));
    return g;
}

bool same(const SteerTables& a, const SteerTables& b) {
    bool s = a.mode == b.mode && a.scale == b.scale && a.fixed_aim == b.fixed_aim && a.compliance_on == b.compliance_on;
    for (size_t k = 0; k < 2; ++k)
        s = s && a.limit[k].bits == b.limit[k].bits && a.spring[k].bits == b.spring[k].bits && a.damping[k].bits == b.damping[k].bits;
    s = s && a.compliance_max.bits == b.compliance_max.bits && a.compliance_spring.bits == b.compliance_spring.bits &&
        a.compliance_damping.bits == b.compliance_damping.bits;
    auto table = [&](const ex::Table<2>& p, const ex::Table<2>& q) {
        s = s && p.n == q.n;
        for (size_t k = 0; k < 2; ++k) s = s && p.x[k].bits == q.x[k].bits && p.y[k].bits == q.y[k].bits;
    };
    table(a.age_curve, b.age_curve), table(a.dead_band, b.dead_band), table(a.compliance_gain, b.compliance_gain);
    for (size_t k = 0; k < 3; ++k) s = s && a.lean_gain[k].bits == b.lean_gain[k].bits;
    return s;
}

void steer_params(SteerExtra& x, const Spec& s, const ex::Params& p, const BodyExtra& body, const WheelLayout& l) {
    for (size_t k = 0; k < 2; ++k)
        x.toe[k] = ex::atan2(behaviour(), (int_to_f(s32(s.b(0x8E + u32(k))) - 0x80) * kHalf) * kThousandth, p.tire_radius[k]);
    x.rear_steer = s.b(0x2C);
    x.rear_ratio = int_to_f(s.b(0x2D)) * kHundredth;
    x.lean_row = s.b(0x23);
    // The sprung mass: the mass less the wheels' (each axle's wheel mass x its wheels).
    const F m0 = body.axle[0].wheel_mass, m1 = body.axle[1].wheel_mass;
    F sprung;
    if (l.axle_wheels[0] == 2 && l.axle_wheels[1] == 2) {
        const F both = m0 + m1;
        sprung = p.mass - (both + both);
    } else
        sprung = p.mass - (m0 * int_to_f(l.axle_wheels[0]) + m1 * int_to_f(l.axle_wheels[1]));
    x.axle_load = {}, x.compliance_base = {}, x.compliance_per_n = {};
    if (sprung <= k0) return;
    const F weight = sprung * kGravity;
    x.axle_load[1] = weight * p.rear_weight;
    x.axle_load[0] = weight * body.front_share;
    x.compliance_base[0] = int_to_f(s.b(0x38)), x.compliance_base[1] = int_to_f(s.b(0x39));
    F front = x.axle_load[0], rear = x.axle_load[1];
    if (s.sh(0x3A) != 0) {
        const F w = int_to_f(s.sh(0x3A)) * kGravity;
        rear = w * p.rear_weight;
        front = w * body.front_share;
    }
    x.compliance_per_n[0] = int_to_f(l.axle_wheels[0]) / front;
    x.compliance_per_n[1] = int_to_f(l.axle_wheels[1]) / rear;
}

F wheel_angle(const ex::Car& c, const SteerExtra& x, const WheelLayout& l, int w, F delta) {
    const size_t i = size_t(w);
    const u8 side = l.side[i];
    const s8 fa = fore_aft(l, w);
    F a = k0;
    if (fa > 0) {
        a = delta;
        if (side < 2) a = a * side_ratio(c, side, delta);
    } else if (fa < 0 && x.rear_steer == 1) {
        a = -delta * x.rear_ratio;
        if (side < 2) a = a * side_ratio(c, side, delta);
    }
    const F toe = x.toe[w < l.count ? l.axle[i] : 0];
    if (side == 0) a = a - toe;
    else if (side == 1) a = a + toe;
    return a;
}

F lean_angle(const ex::Car& c, const SteerExtra& x, const WheelLayout& l, const SteerTables& g, int w) {
    if (!(fore_aft(l, w) > 0)) return k0;
    // 0x359F50: the row of the parameter block's byte +0x33 (0 past 2); 0x35A008: cos a x (3 - 2 cos b).
    const F gain = gt3r::ee::cos(g.lean_gain[x.lean_row < 3 ? x.lean_row : 0]);
    const F lean = gt3r::ee::cos(c.roll - c.ground_roll);
    return c.steer_cmd * (gain * (kThree - (lean + lean)));
}

void follow_steer(const ex::Car& c, SteerExtra& x) {
    const F aim = c.steer_cmd * kFollowGain;
    const F a = (aim - x.follow) * kFollowSpring - x.follow_rate * kFollowDamp;
    x.follow_accel = a;
    x.follow_rate = x.follow_rate + a * kTick;
    x.follow = x.follow + x.follow_rate * kTick;
}

F angle_diff(F a, F b) { return ex::angle_diff(a, b, kWrap); }

F angle_blend(F wa, F a, F wb, F b) {
    a = ex::angle_wrap(a, kWrap), b = ex::angle_wrap(b, kWrap);
    const F d = a - b;
    if (kPi <= d) b = b + kTwoPi;
    else if (d < kMinusPi) a = a + kTwoPi;
    return ex::angle_wrap(wa * a + wb * b, kWrap);
}

F offset_aim(const ex::Car& c, const TireExtra& t, const WheelLayout& l, const SteerTables& g, int w) {
    const size_t i = size_t(w);
    const ex::Wheel& wh = c.w[i];
    const bool fixed = wh.surface < g.fixed_aim.size() && g.fixed_aim[wh.surface] == 1;
    const F off = t.steer_offset[i];
    if (g.mode == 2) {
        if (fixed) return wh.steer;
        if (wh.slip_fade == k0) return off;
        if (wh.hub_vx < k0) return angle_blend(kHalf, wh.slip_dir - kPi, kHalf, wh.steer);
        return (wh.slip_dir + wh.steer) * kHalf;
    }
    if (fixed) return k0;
    if (wh.slip_fade == k0) { // the offset taken back by half the steer angle's change since the last step, within the limit
        const F lim = g.limit[l.geometry[i]];
        const F aim = off - (wh.steer - c.prev.steer[i]) * kHalf;
        if (aim < -lim) return -lim;
        if (lim < aim) return lim;
        return aim;
    }
    if (wh.hub_vx < k0) return angle_diff(wh.slip_dir - kPi, wh.steer) * kHalf;
    return (wh.slip_dir - wh.steer) * kHalf;
}

void steer_offset(ex::Car& c, SteerExtra& x, TireExtra& t, const WheelLayout& l, const SteerTables& g, int w) {
    const size_t i = size_t(w), k = l.geometry[i];
    ex::Wheel& wh = c.w[i];
    const F aim = offset_aim(c, t, l, g, w);
    const F off = t.steer_offset[i], rate = x.offset_rate[i];
    F push = g.spring[k] * (aim - off) - g.damping[k] * rate;
    F lim = g.limit[k];
    const s32 scale = s32(g.scale);
    if (scale != 0) {
        const F share = (wh.peak_force * int_to_f(l.group_wheels[k])) / x.axle_load[k];
        if (scale == 2 || scale == 3) push = push * share;
        if (scale == 1 || scale == 3) lim = lim * share;
    }
    x.offset_rate[i] = rate + push * c.dt;
    t.steer_offset[i] = off + x.offset_rate[i] * c.dt;
    const F o = t.steer_offset[i];
    if (g.mode == 2) {
        const F d = o - wh.steer;
        if (lim < d) t.steer_offset[i] = wh.steer + lim, x.offset_rate[i] = k0;
        else if (d < -lim) t.steer_offset[i] = wh.steer - lim, x.offset_rate[i] = k0;
    } else {
        if (lim < o) t.steer_offset[i] = lim, x.offset_rate[i] = k0;
        else if (o < -lim) t.steer_offset[i] = -lim, x.offset_rate[i] = k0;
    }
}

void compliance_steer(ex::Car& c, SteerExtra& x, const WheelLayout& l, const SteerTables& g, int w) {
    const size_t i = size_t(w), k = l.geometry[i];
    ex::Wheel& wh = c.w[i];
    const F force = wh.force_a * x.compliance_per_n[k];
    const F base = x.compliance_base[k] + ex::lookup_bisect(g.age_curve, F::raw(c.x408[0]));
    const F dead = ex::lookup_bisect(g.dead_band, base);
    F aim;
    if (k0 <= force) {
        aim = force - dead;
        if (aim < k0) aim = k0;
    } else {
        aim = force + dead;
        if (k0 < aim) aim = k0;
    }
    if (!(aim == k0)) {
        aim = aim * ex::lookup_bisect(g.compliance_gain, base);
        const F m = g.compliance_max;
        if (m < aim) aim = m;
        else if (aim < -m) aim = -m;
        if (k == 1) aim = -aim;
    }
    const F now = x.compliance[i];
    const F push = g.compliance_spring * (aim - now) - g.compliance_damping * x.compliance_rate[i];
    x.compliance_rate[i] = x.compliance_rate[i] + push * c.dt;
    x.compliance[i] = now + x.compliance_rate[i] * c.dt;
    wh.steer = wh.steer + x.compliance[i];
}

void wheel_steer(ex::Car& c, SteerExtra& x, TireExtra& t, const WheelLayout& l, const SteerTables& g) {
    const int n = count(l);
    for (int w = 0; w < n; ++w) c.w[size_t(w)].steer = l.kind == 1 ? lean_angle(c, x, l, g, w) : wheel_angle(c, x, l, w, c.steer_cmd);
    follow_steer(c, x);
    if (g.mode != 0)
        for (int w = 0; w < n; ++w) steer_offset(c, x, t, l, g, w);
    if (g.compliance_on != 0)
        for (int w = 0; w < n; ++w) compliance_steer(c, x, l, g, w);
}

} // namespace gt4r::sim::rules
