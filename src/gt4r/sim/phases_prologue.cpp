// Gran Turismo 4 Prologue's own routines of its race step (phases_prologue.h).

#include "gt4r/sim/phases_prologue.h"

#include "base/ee_double.h"
#include "base/ee_math.h"
#include "gt4r/sim/feel_prologue.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/program_prologue.h"
#include "gt4r/sim/rules_phases.h"
#include "gt4r/sim/rules_tires.h"
#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

namespace gt4r::sim::prologue {

namespace {
const F kOne = F::raw(0x3F800000);
const F kMilli = F::raw(0x3A83126E); // 0.001 (the later builds' rounding)
F f_of(s32 v) { return gt3r::ee::int_to_f(v); }
using gt3r::s8;
s8 low_byte(s32 v) { return s8(u8(u32(v))); }
// 1 - a - b in the program's soft doubles (0x4B5AC0, 0x4B5DB8, 0x4B63C0).
F one_minus(const ex::Behaviour& h, F a, F b) {
    namespace bm = gt3r::sim::exact;
    return bm::narrow(h, bm::sub(h, bm::sub(h, gt3r::ee::D::raw(0x3FF0000000000000ull), bm::widen(h, a)), bm::widen(h, b)));
}
} // namespace

StepTables step_tables(const Program& program) {
    StepTables t;
    const u32 s = PrologueProgram::kStatic;
    const auto b = [&](u32 o) { return program.b(s + o); };
    // 0x3DBA58
    if (const u8 b0 = b(0)) {
        const F d = f_of(b0);
        t.slip.drag_share = f_of(b(1)) / d, t.slip.downforce_share = f_of(b(2)) / d;
    }
    const F ten = F::raw(0x41200000);
    t.slip.k_up = b(5) ? ten / f_of(b(5)) : kOne;
    t.slip.k_down = b(6) ? ten / f_of(b(6)) : F::raw(0x3E800000);
    // 0x3B9530
    ex::Condition& k = t.condition;
    const auto tenk = [&](u32 v) { return f_of(s32(v * 10000)); };
    k.c1 = f_of(b(0xA1)) * kMilli;
    k.e1 = -tenk(b(0xA0));
    k.e2 = tenk(b(0xA2));
    k.e3 = tenk(b(0xA3)) + k.e2;
    k.c2 = f_of(b(0xA4)) * kMilli;
    k.co = f_of(b(0xA6)) * kMilli;
    k.o1 = -tenk(b(0xA7));
    k.omax = tenk(u32(b(0xA5)) + b(0xA7));
    k.x20 = tenk(b(0xA8));
    const auto w = [&](u32 a) { return F::raw(program.w(a)); };
    t.flap_accel = w(0x50430C), t.flap_speed = w(0x504310), t.flap_chance = w(0x504314), t.flap_rim = w(0x504318);
    t.flap_rough = w(0x50431C), t.flap_spring = w(0x504320), t.flap_damp = w(0x504324);
    return t;
}

bool wear_grip(ex::Car& c, const CarPart& x, const ex::WearParams& k) {
    if (k.max == F()) return false;
    if (c.x535 & 0x10) return false;
    const F t63 = F::raw(0x427F9999), t31 = F::raw(0x41FF3333);
    for (int i = 0; i < x.layout.count && i < 4; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        const F v = w.wear;
        if (k.max <= v) {
            w.wear = k.max;
            w.wear_level = 0x7F;
            w.wear_grip = kOne - k.max_loss;
        } else if (k.knee2 <= v) {
            const F t = (v - k.knee2) / (k.max - k.knee2);
            w.wear_grip = one_minus(c.behaviour, k.loss2, t * (k.max_loss - k.loss2));
            w.wear_level = kOne <= t ? s8(0x7F) : low_byte(gt3r::ee::to_int(t * t63) + 0x40);
        } else if (k.knee1 <= v) {
            const F t = (v - k.knee1) / (k.knee2 - k.knee1);
            w.wear_grip = one_minus(c.behaviour, k.loss1, t * (k.loss2 - k.loss1));
            w.wear_level = kOne <= t ? s8(0x40) : low_byte(gt3r::ee::to_int(t * t31) + 0x20);
        } else if (F() <= v) {
            const F t = v / k.knee1;
            w.wear_grip = kOne - k.loss1 * t;
            w.wear_level = kOne <= t ? s8(0x20) : low_byte(gt3r::ee::to_int(t * t31));
        } else if (k.fresh <= v) {
            const F t = v / k.fresh;
            w.wear_grip = kOne - k.fresh_loss * t;
            w.wear_level = kOne <= t ? s8(-0x3F) : low_byte(-gt3r::ee::to_int(t * t63));
        } else {
            w.wear_level = s8(-0x80);
            w.wear_grip = kOne;
        }
        const F floor = rules::tire_record(x.tires, x.layout, i, w.surface).mu[11];
        if (w.wear_grip < floor) w.wear_grip = floor;
    }
    return true;
}

void peak_force(ex::Car& c, const CarPart& x, bool wear) {
    const rules::WheelLayout& l = x.layout;
    const F d = c.roll - c.ground_roll;
    for (int i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        const rules::Tire& r = rules::tire_record(x.tires, l, i, w.surface);
        const size_t a = l.axle[size_t(i)] & 1u;
        F f = w.load * rules::table(r.load_curve, rules::load_per_width(x.tires, l, i, w.load));
        if (wear) f = f * w.wear_grip;
        f = f * c.p.axle_grip[a];
        F camber = F();
        if (l.kind != 1) {
            const u8 side = l.side[size_t(i)];
            camber = (side == 0 ? -d : side == 1 ? d : F()) + c.p.camber[a];
        }
        const F g = rules::table(c.p.camber_grip[a], camber);
        w.peak_force = f * r.mu[w.surface % 24] * g;
    }
}

void aero(ex::Car& c, const CarPart& x, const FeelPart& f, const StepTables& t) {
    const ex::Slipstream& s = t.slip;
    const F v2 = c.speed * c.speed;
    if (c.slip_target == F()) {
        const F y = c.slip_filter - s.k_down * c.dt;
        c.slip_filter = y < F() ? F() : y;
    } else {
        const F y = c.slip_filter + c.slip_target * s.k_up * c.dt;
        c.slip_filter = kOne < y ? kOne : y;
    }
    const F u = kOne - c.slip_filter;
    const F d = c.p.drag_coef * ((s.drag_share + (kOne - s.drag_share) * u) * v2);
    c.drag = F() <= c.speed ? -d : d;
    const F a = (s.downforce_share + (kOne - s.downforce_share) * u) * v2;
    for (size_t j = 0; j < 2; ++j) c.downforce[j] = c.p.downforce_coef[j] * a;
    part_downforce(c, x, f, s);
}

F condition_factor(const ex::Condition& k, F e, F o) {
    F fe = kOne;
    if (e < k.e1) fe = kOne - k.c1;
    else if (e < F()) fe = kOne - e * k.c1 / k.e1;
    else if (k.e2 < e) fe = e < k.e3 ? kOne - (e - k.e2) * k.c2 / (k.e3 - k.e2) : kOne - k.c2;
    F fo = kOne;
    if (o < k.o1) fo = k.co + kOne;
    else if (o < F()) fo = o * k.co / k.o1 + kOne;
    return fe * fo;
}

void prep(ex::Car& c, const CarPart& x, const FeelPart& f, const StepTables& t, s32 start_count, const ex::WearParams& wear) {
    c.race_hold = start_count > 0 ? 1 : 0;
    if (c.hold) --c.hold;
    if (c.x51d != 0 && c.x51d != 0xFF) --c.x51d;
    rules::step_length(c);
    aero(c, x, f, t);
    c.torque_wot = ex::lookup_bisect(c.p.e.torque, c.omega) * prologue::condition_factor(t.condition, c.engine_wear, c.oil);
    c.torque_friction = c.p.e.friction * c.omega;
    peak_force(c, x, wear_grip(c, x, wear));
    rules::slip_angles(c, x.layout);
}

void tire_wear(ex::Car& c, const CarPart& x, const ex::WearParams& k) {
    const rules::WheelLayout& l = x.layout;
    if (k.max == F()) return;
    const int n = l.count < 4 ? l.count : 4;
    for (int i = 0; i < n; ++i) c.w[size_t(i)].wear_byte = 0;
    if (c.x535 & 0x10) return;
    const F dt = c.dt, k255 = F::raw(0x437F0000);
    for (int i = 0; i < n; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        const size_t a = l.axle[size_t(i)] & 1u;
        if (w.surface == 1) continue;
        F u = w.load;
        if (u == F()) continue;
        u = c.p.wear_load_offset[a] + u * c.p.wear_load_gain[a];
        if (u < F()) u = F();
        const rules::Tire& r = rules::tire_record(x.tires, l, i, w.surface);
        F s = w.slip_intensity;
        if (k.slip_cap < s) s = k.slip_cap;
        s = s * r.wear_rate;
        const F d = s * u;
        s = s * k255;
        w.wear = w.wear + d * dt;
        if (k255 < s) s = k255;
        w.wear_byte = u8(gt3r::ee::to_int(s));
    }
}

void wear_flags(ex::Car& c, const CarPart& x, const ex::WearParams& k, const PitRule& r) {
    const rules::WheelLayout& l = x.layout;
    if (!r.pits || (c.x535 & 0x08)) return;
    const int n = l.count < 4 ? l.count : 4;
    u32 f = 0;
    if (!(k.max == F()))
        for (int i = 0; i < n; ++i) {
            const ex::Wheel& w = c.w[size_t(i)];
            if (w.wear < F()) continue;
            if (w.wear_grip < k.worn) {
                f = 1;
                break;
            }
        }
    const s32 mode = c.control != 0 ? 0 : r.mode;
    if (mode == 4)
        for (int i = 0; i < n; ++i)
            if (c.w[size_t(i)].x28) {
                f |= 2;
                break;
            }
    if (!f) return;
    const u8 flags = c.x535 | 0x04;
    c.x535 = flags;
    if (c.control == 1 || c.control == 2) c.x535 = flags | 0x08;
}

void flaps(ex::Car& c, const CarPart& x, const StepTables& t, gt3r::Vu0Random& rng) {
    const rules::WheelLayout& l = x.layout;
    for (int i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        F a = c.a_x * t.flap_accel;
        F p = w.flap, v = w.flap_v;
        const F room = F() <= w.hub_vx ? kOne - p : p + kOne;
        if (ex::vu0_random(rng) * room < t.flap_chance) a = a + (kOne - p) * t.flap_speed * w.hub_vx;
        const size_t ax = l.axle[size_t(i)] & 1u;
        if (x.tires.tire_index[ax][w.surface % 24] != 0 && F() < w.hub_vx && F() < w.load) {
            const F left = kOne - p;
            if (ex::vu0_random(rng) * left < t.flap_rough) a = a + left * t.flap_rim * w.rim_speed;
        }
        const F dt = c.dt;
        a = a - p * t.flap_spring;
        a = a - v * t.flap_damp;
        v = v + a * dt;
        p = p + v * dt;
        if (kOne < p) p = kOne, v = F();
        else if (p < F::raw(0xBF800000)) v = F(), p = F::raw(0xBF800000);
        w.flap_v = v;
        w.flap = p;
    }
}

} // namespace gt4r::sim::prologue
