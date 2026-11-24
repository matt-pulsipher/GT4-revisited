// Gran Turismo 4's own routines of the race step: the pad's vibration, the view's follower and the body's sway
// (rules_feel.h).

#include "gt4r/sim/rules_feel.h"

#include "gt4r/sim/program.h"
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

FeelTables feel_tables(const Program& program) {
    FeelTables t;
    t.view_heading_gain = F::raw(program.w(0x69F4C8)), t.view_heading_damp = F::raw(program.w(0x69F4D0));
    u8 b[10];
    for (u32 k = 0; k < 10; ++k) b[k] = program.b(kSwayBytes + k);
    t.sway_pitch_on = b[0], t.sway_roll_on = b[5];
    const std::array<F, 8> k = sway_constants(b);
    for (size_t j = 0; j < 4; ++j) t.sway_pitch[j] = k[j], t.sway_roll[j] = k[4 + j];
    return t;
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
