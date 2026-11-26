// Gran Turismo 4's own routines of the race step: the pad's vibration, the view's follower and the body's sway
// (rules_feel.h).

#include "gt4r/sim/rules_feel.h"

#include "gt4r/sim/program.h"
#include "gt4r/sim/rules_effects.h"
#include "gt4r/sim/rules_steer.h"

namespace gt4r::sim::rules {

namespace {
const F kZero = F(), kOne = F::raw(0x3F800000), kHalf = F::raw(0x3F000000), kQuarter = F::raw(0x3E800000);
const F kStep = F::raw(0x3C888888); // 1/60
constexpr gt3r::sim::Program kWrap = gt3r::sim::Program::ConceptGenevaAsia; // 0x36CB90's turn constants

// 0x344858 .. 0x3449A8: a value at the step's blend between the last step's and this one's.
F blended(F last, F now, F blend) {
    const F b = kZero + blend;
    return last * b + now * (kOne - b);
}

void follow(F d, const std::array<F, 4>& k, F& v, F& x) {
    const F lim = k[0];
    if (lim < d) d = lim;
    else if (d < -lim) d = -lim;
    F a = k[2] * (d * k[1] - x) - k[3] * v;
    v = v + a * kStep;
    x = x + v * kStep;
}
} // namespace

std::array<F, 8> sway_constants(const u8* b) {
    using gt3r::ee::int_to_f;
    const F deg = F::raw(0x3AE4C385), tenth = F::raw(0x3DCCCCCC); // 0.1 degree in radians, 0.1
    return {int_to_f(b[1]) * deg, int_to_f(b[2]) * tenth, int_to_f(b[3]), int_to_f(b[4]),
            int_to_f(b[6]) * deg, int_to_f(b[7]) * tenth, int_to_f(b[8]), int_to_f(b[9])};
}

std::array<F, 32> cue_table(const u8* b) {
    using gt3r::ee::int_to_f;
    const F ten = F::raw(0x41200000), hundred = F::raw(0x42C80000), kmh = F::raw(0x4031C71B), g = F::raw(0x411CCCCC), krpm = F::raw(0x4482E651);
    auto u = [&](u32 o) { return int_to_f(b[o]); };
    std::array<F, 32> t{};
    const F e = u(0xE) / ten;
    const F f = u(0xF) / ten / hundred;
    t[0] = e * f * f;
    t[1] = u(0x10) * kmh, t[2] = u(0x11) * kmh;
    t[3] = int_to_f(s32(b[0x12]) * 100), t[4] = int_to_f(s32(b[0x13]) * 100);
    t[5] = u(0x14) / ten;
    const F h = u(0x16) / ten / hundred;
    t[6] = e * h * h;
    t[7] = u(0x17) * kmh, t[8] = u(0x18) * kmh;
    t[9] = u(0x19) / hundred, t[10] = u(0x1A) / hundred, t[11] = u(0x1B) / ten;
    t[12] = u(6) / hundred, t[13] = u(7) / hundred, t[14] = u(8) / hundred;
    t[15] = u(9) * kmh, t[16] = u(0xA) * kmh;
    t[17] = u(0xB) / hundred, t[18] = u(0xC) / hundred, t[19] = u(0xD) / ten;
    t[20] = u(0) / hundred, t[21] = u(1) / hundred, t[22] = u(2) * g;
    t[24] = u(4) / ten;
    t[23] = (u(3) / ten - t[24]) / krpm;
    t[25] = u(0x1C) / hundred, t[26] = u(0x1D) / hundred, t[27] = u(0x1E) / hundred, t[28] = u(0x1F) / ten;
    t[29] = u(0x26) / hundred, t[30] = u(0x28) / hundred, t[31] = u(0x29) / hundred;
    return t;
}

FeelTables feel_tables(const Program& program) {
    FeelTables t;
    t.view_heading_gain = F::raw(program.w(0x69F4C8)), t.view_heading_damp = F::raw(program.w(0x69F4D0));
    u8 b[10];
    for (u32 k = 0; k < 10; ++k) b[k] = program.b(kSwayBytes + k);
    t.sway_pitch_on = b[0], t.sway_roll_on = b[5];
    const std::array<F, 8> k = sway_constants(b);
    for (size_t j = 0; j < 4; ++j) t.sway_pitch[j] = k[j], t.sway_roll[j] = k[4 + j];
    std::array<u8, 0x210> raw{};
    for (u32 j = 0; j < raw.size(); ++j) raw[j] = program.b(kSlideBytes + j);
    t.cue = cue_table(effect_bytes(raw.data()).data() + 0x1E0);
    for (u32 j = 0; j < t.jolt_bytes.size(); ++j) t.jolt_bytes[j] = program.b(kSwayBytes + 0xA + j);
    return t;
}

JoltLimits jolt_limits(const FeelTables& t, u8 course_kind) {
    const std::array<u8, 7>& b = t.jolt_bytes; // 0x6202BA ..
    JoltLimits l;
    l.on = b[0] != 0;
    s32 lo = b[1], hi = b[2];
    if (lo == hi) hi = lo + 1;
    const s32 raise = course_kind == 2 ? b[6] : course_kind == 3 ? b[5] : 0;
    lo += raise, hi += raise;
    const F kmh = F::raw(0x3E8E38E3); // 1 / 3.6
    l.floor = gt3r::ee::int_to_f(lo) * kmh, l.cap = gt3r::ee::int_to_f(hi) * kmh;
    l.base = gt3r::ee::int_to_f(b[3]) * kmh;
    const F top = gt3r::ee::int_to_f(b[4]) * kmh;
    l.gain = (top - l.base) / (l.cap - l.floor);
    return l;
}

void view_jolt(const ex::Car& c, const ex::Vec3& after, ViewState& v, const JoltLimits& l) {
    if (!l.on) return;
    if (v.jolt != 0) {
        v.jolt = 0;
        return;
    }
    const F d[3] = {after.x - c.vel.x, after.y - c.vel.y, after.z - c.vel.z};
    const F fwd[3] = {c.fwd.x, c.fwd.y, c.fwd.z}, lat[3] = {c.lat.x, c.lat.y, c.lat.z};
    F along = F(), across = F();
    for (int k = 0; k < 3; ++k) along = along + fwd[k] * d[k], across = across + lat[k] * d[k];
    F size = sqrt(along * along + across * across);
    if (!(l.floor < size)) return;
    F share = F(1.0f);
    if (l.cap < size) share = l.cap / size, size = l.cap;
    const F kept = l.base + l.gain * (size - l.floor);
    share = share * (kept / size);
    v.jolt = 1;
    v.jolt_forward = along * share, v.jolt_side = across * share;
}

void view_start(const ex::Car& c, ViewState& v) {
    v = ViewState{};
    v.heading = c.heading, v.pitch = c.pitch;
}

void view_follow(ex::Car& c, ViewState& v, const FeelTables& t) {
    F a = angle_diff(c.heading, v.heading) * t.view_heading_gain;
    a = a - v.heading_rate * t.view_heading_damp;
    const F m = (v.heading_acc + a) * kHalf * kStep;
    v.heading_acc = a;
    v.heading_rate = v.heading_rate + m;
    v.heading = ex::angle_wrap(v.heading + v.heading_rate * kStep, kWrap);
    c.lag_heading_diff = angle_diff(c.heading, v.heading);
    F p = angle_diff(c.pitch, v.pitch) * F::raw(0x41A00000) - v.pitch_rate * F::raw(0x41200000); // 20, 10
    v.pitch_rate = v.pitch_rate + p * kStep;
    v.pitch = ex::angle_wrap(v.pitch + v.pitch_rate * kStep, kWrap);
    c.lag_pitch_diff = angle_diff(c.pitch, v.pitch);
}

void sway(const ex::Car& c, ViewState& v, const FeelTables& t, F blend) {
    if (t.sway_pitch_on) {
        const F d = blended(c.prev.pitch, c.pitch, blend) - blended(c.prev.ground_pitch, c.ground_pitch, blend);
        follow(d, t.sway_pitch, v.sway_pitch_rate, v.sway_pitch);
    }
    if (t.sway_roll_on) {
        const F d = blended(c.prev.roll, c.roll, blend) - blended(c.prev.ground_roll, c.ground_roll, blend);
        follow(d, t.sway_roll, v.sway_roll_rate, v.sway_roll);
    }
}

namespace {
// A level's byte: truncated, 127 from 128 on.
s32 level127(F v) {
    const s32 n = gt3r::ee::to_int(v);
    return n < 0x80 ? n : 0x7F;
}
u8 mean_level(u8 last, s32 now) {
    const s32 v = s32(last) + now;
    return u8((v + s32(u32(v) >> 31)) >> 1);
}
} // namespace

void engine_sound(ex::Car& c, const FeelTables& t, u8 gearbox_kind, int draw_mode) {
    if (draw_mode != 3) {
        c.blowoff = kZero, c.x4e5 = 0, c.turbo_spool = 0, c.x4e4 = 0;
        return;
    }
    const bool hybrid = gearbox_kind == 3;
    const F idle = gt3r::ee::int_to_f(c.p.e.idle) * F::raw(0x3DD6774E); // rpm in rad/s
    if (hybrid && c.omega < idle) {
        c.x4e4 = 0, c.x4e5 = 0;
        return;
    }
    F load = c.turbo_load;
    if (kOne < load) load = kOne;
    const F share = c.torque_friction / (c.torque_friction + c.torque_wot);
    F d = load - share;
    F a = d < kZero ? -d : d;
    a = a + share;
    if (c.clutch != 1) a = a * kHalf;
    if (hybrid) a = a * kHalf;
    const F k127 = F::raw(0x42FE0000);
    c.x4e4 = mean_level(c.x4e4, level127(a * k127 * t.cue[25]));
    if (d < kZero) d = kZero;
    d = d / (kOne - share);
    d = kOne - d;
    d = d * d, d = d * d, d = d * d, d = d * d;
    d = kOne - d;
    d = d * F::raw(0x3F199999) + F::raw(0x3ECCCCCC);
    if (hybrid) d = d * kHalf;
    c.x4e5 = mean_level(c.x4e5, level127(d * k127 * t.cue[26]));
    c.turbo_spool = u8(level127(gt3r::ee::int_to_f(c.turbo_spool) * t.cue[27]));
    c.blowoff = c.blowoff * t.cue[28];
}

void pad_rumble(const ex::Car& c, const WheelLayout& l, std::array<u8, 2>& pad) {
    if (u8(u8(c.control) - 1) < 2 || c.driver_kind != 0) {
        pad = {0, 0};
        return;
    }
    const int n = l.count < 4 ? l.count : 4;
    F drive = kZero, brake = kZero, slip = kZero;
    int ahead = 0;
    for (int i = 0; i < n; ++i) {
        if (s8(l.fore_aft[size_t(i)]) <= 0) continue;
        const ex::Wheel& w = c.w[size_t(i)];
        ++ahead;
        brake = brake + w.fx_brake;
        drive = drive + w.fx_drive;
        slip = slip + w.slip_ratio * w.slip_ratio;
    }
    if (ahead != 0) drive = drive * (slip / gt3r::ee::int_to_f(ahead) + F::raw(0x3DCCCCCC));
    if (drive < kZero) drive = -drive;
    const F cap = F::raw(0x3EAA7EF9), scale = F::raw(0x46991FFF); // 0.333, 19,600
    drive = drive * F::raw(0x3A2A7EF9);
    if (cap < drive) drive = cap;
    brake = brake / scale;
    brake = brake * brake;
    if (cap < brake) brake = cap;
    F hit = kZero;
    if (F::raw(0x3C23D70A) < c.hit_rumble) hit = c.hit_rumble * F::raw(0x41000000) + kQuarter;
    const F forces = drive + brake;
    F bump = kZero, kerb = kZero;
    for (int i = 0; i < n; ++i) {
        if (c.race_hold != 0) continue;
        const ex::Wheel& w = c.w[size_t(i)];
        if (kZero < w.damper_f) {
            const F x = w.damper_f / scale;
            bump = bump + (kOne < x ? kOne : x);
        }
        if (s8(l.fore_aft[size_t(i)]) <= 0) continue;
        const u8 s = w.surface;
        if (s != 1 && s != 10 && s != 11 && s != 21) continue;
        F k = w.load / F::raw(0x4537BFFF) * (c.speed * F::raw(0x3C800000)); // 2,940 N, / 64
        if (k < kZero) k = -k;
        kerb = kerb + (kQuarter < k ? kQuarter : k);
    }
    F level = bump + kerb;
    if (level < forces) level = forces;
    if (level < hit) level = hit;
    s32 v = gt3r::ee::to_int(level * F::raw(0x43800000));
    if (!(v < 0x80)) v = 0x7F;
    pad[1] = u8(v);
}

} // namespace gt4r::sim::rules
