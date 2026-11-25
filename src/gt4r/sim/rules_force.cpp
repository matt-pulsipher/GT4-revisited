// Gran Turismo 4's own routines of the race step: the steering wheel's force (rules_force.h).

#include "gt4r/sim/rules_force.h"

#include "base/ee_math.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/record.h"

namespace gt4r::sim::rules {

namespace {

using gt3r::ee::int_to_f;

const F k0 = F::raw(0), kOne = F::raw(0x3F800000), kMinusOne = F::raw(0xBF800000), kHalf = F::raw(0x3F000000);
const F kTwo = F::raw(0x40000000);
const F kFiftieth = F::raw(0x3CA3D70A), kHundredth = F::raw(0x3C23D70A), kTenth = F::raw(0x3DCCCCCC); // 0.02, 0.01, 0.1
const F kStick = F::raw(0x39800000);      // 1 / 4096
const F kTenthDeg = F::raw(0x3AE4C385);   // pi / 1800
const F kDeg = F::raw(0x3C8EFA34);        // pi / 180
const F kEight = F::raw(0x41000000), kHundred = F::raw(0x42C80000), k255th = F::raw(0x40233333); // 8, 100, 2.55
const F kUnits = F::raw(0x46000000);      // 8192: the steering's units over a whole lock
const F kTurn0 = F::raw(0x43480000), kTurn6 = F::raw(0x44610000), kSixty = F::raw(0x42700000); // 200, 900 degrees; 60
const F kSlow = F::raw(0x3F0E38E3);       // 2 km/h in m/s
const F kSpring = F::raw(0x473F67FF), kSpringRef = F::raw(0x483F67FF); // 49,000, 196,000
const F kSlowMax = F::raw(0x3DB2B8C1), kSlowGain = F::raw(0x40B758B5); // 5 degrees, 1 / 10 degrees
const F kThirtieth = F::raw(0x3D088888), k127 = F::raw(0x42FE0000);

F f_of(s32 v) { return int_to_f(v); }

// 0x359568 on the mode's three-point table (fewer than four points: a scan; an x at or past an end gives that end's y).
F rate_factor(const ForceMode& k, F v) {
    const s32 n = k.rate_n < 3 ? k.rate_n : 3;
    if (n < 1) return k0;
    const u32 last = u32(n - 1);
    if (v <= k.rate_x[0]) return k.rate_y[0];
    if (k.rate_x[last] <= v) return k.rate_y[last];
    u32 i = 0;
    if (i < last && !(v < k.rate_x[i])) {
        ++i;
        while (i < last && !(v < k.rate_x[i])) ++i;
    }
    const F x0 = k.rate_x[i - 1], x1 = k.rate_x[i], y0 = k.rate_y[i - 1], y1 = k.rate_y[i];
    return y0 + ((y1 - y0) * (v - x0)) / (x1 - x0);
}

// 0x3F7C90: the word 0x845BF8 + 4 i (the offset's limits, and past them the settings' springs and dampings).
F offset_limit(const SteerTables& g, u8 i) {
    if (i < 2) return g.limit[i];
    if (i < 4) return g.spring[size_t(i - 2)];
    if (i < 6) return g.damping[size_t(i - 4)];
    return k0;
}

void clamp1(F& v) {
    if (kOne < v) v = kOne;
    else if (v < kMinusOne) v = kMinusOne;
}

} // namespace

ForceMode force_mode(const u8* K, F r61, F r62, int m) {
    const u8* S = K + 0x7C + kForceRowSize * u32(m);
    ForceMode k;
    k.gain[0] = f_of(S[0]) * kFiftieth;
    k.align_gain = f_of(S[3]) * kTenth;
    k.gain[1] = f_of(S[0x14]) * kFiftieth;
    k.curve[0] = pedal_curve_of(S[1], S[2]);
    k.curve[1] = pedal_curve_of(S[0x15], S[0x16]);
    k.turn_gain = f_of(S[5]) * kHundredth * kStick;
    k.steer_max = f_of(S[7]) * kTenthDeg;
    k.steer_gain = S[6] == 0 ? k0 : (f_of(S[6]) * kHundredth) / k.steer_max;
    k.offset_gain = f_of(S[0x10]) * kHundredth;
    k.offset_damp = (f_of(s8(S[0x11])) * kHundredth) / kDeg;
    k.offset_curve = pedal_curve_of(S[0x12], S[0x13]);
    if (u32(S[0xA]) + S[0xB] != 0 && S[0xC] != 0) {
        const F a = f_of(S[0xA]), b = f_of(S[0xB]), sum = a + b;
        const F c = f_of(S[0xC]) * kDeg;
        k.drive_share = f_of(S[0xD]) / sum;
        k.slip_base = a / sum, k.slip_share = b / sum;
        k.slip_inv = kOne / c;
        k.slip_on = 1;
    }
    k.x64 = f_of(K[0x74]) / kHundred;
    k.x6c = kEight / r62, k.x68 = kEight / r61;
    if (K[0x7A] == 0) k.x70 = k0, k.x78 = K[0x75];
    else k.x78 = 0, k.x70 = f_of(K[0x75]) * k255th;
    k.x74 = f_of(K[0x76]) * k255th;
    k.wheel = {K[0x77], K[0x78], K[0x79]};
    k.limit_kind = S[0x17];
    for (u32 j = 0; j < 3; ++j) k.strength[j] = f_of(S[0x18 + j]) * kHundredth;
    const F turn = (m != 0 ? kTurn6 : kTurn0) * kSixty;
    const F x1 = f_of(s32(S[0x1D]) * 10) * kUnits, x2 = f_of(s32(S[0x1E]) * 10) * kUnits;
    k.rate_n = 3;
    k.rate_y = {kOne, f_of(S[0x1B]) * kHundredth, f_of(S[0x1C]) * kHundredth};
    k.rate_x = {k0, x1 / turn, x2 / turn};
    return k;
}

ForceTables force_tables(const Program& program) {
    ForceTables t;
    if (!program.known()) return t;
    u8 K[kForceSettingsSize];
    for (u32 i = 0; i < kForceSettingsSize; ++i) K[i] = program.b(kPadSettings + i);
    const PadTables pad = pad_tables(program);
    for (int m = 0; m < 3; ++m) t.mode[size_t(m)] = force_mode(K, pad.wheel6_ratio[0], pad.wheel6_ratio[2], m);
    t.offset_feel = program.b(kOffsetFeel);
    return t;
}

bool same(const ForceTables& a, const ForceTables& b, std::string* why) {
    bool ok = true;
    if (why) why->clear();
    auto fail = [&](int m, const char* what) {
        if (why) *why += (why->empty() ? "" : ", ") + std::string("mode ") + std::to_string(m) + " " + what;
        ok = false;
    };
    auto eq = [](F x, F y) { return x.bits == y.bits; };
    auto curve = [&](const PedalCurve& x, const PedalCurve& y) { return x.kind == y.kind && eq(x.a, y.a) && eq(x.b, y.b) && eq(x.c, y.c); };
    for (int m = 0; m < 3; ++m) {
        const ForceMode &x = a.mode[size_t(m)], &y = b.mode[size_t(m)];
        if (!eq(x.align_gain, y.align_gain) || !eq(x.gain[0], y.gain[0]) || !eq(x.gain[1], y.gain[1])) fail(m, "gains +0x00");
        if (!curve(x.curve[0], y.curve[0]) || !curve(x.curve[1], y.curve[1])) fail(m, "curves +0x0C");
        if (!eq(x.turn_gain, y.turn_gain) || !eq(x.steer_gain, y.steer_gain) || !eq(x.steer_max, y.steer_max)) fail(m, "steering +0x2C");
        if (!eq(x.offset_gain, y.offset_gain) || !eq(x.offset_damp, y.offset_damp) || !curve(x.offset_curve, y.offset_curve)) fail(m, "offset +0x38");
        if (x.slip_on != y.slip_on) fail(m, "slip switch +0x50");
        else if (x.slip_on && (!eq(x.slip_base, y.slip_base) || !eq(x.slip_share, y.slip_share) || !eq(x.slip_inv, y.slip_inv) ||
                               !eq(x.drive_share, y.drive_share)))
            fail(m, "slip shares +0x54");
        if (!eq(x.x64, y.x64) || !eq(x.x68, y.x68) || !eq(x.x6c, y.x6c) || !eq(x.x70, y.x70) || !eq(x.x74, y.x74) || x.x78 != y.x78)
            fail(m, "words +0x64");
        if (x.wheel != y.wheel || x.limit_kind != y.limit_kind) fail(m, "bytes +0x79");
        for (size_t j = 0; j < 3; ++j)
            if (!eq(x.strength[j], y.strength[j])) fail(m, "strengths +0x80");
        bool rate = x.rate_n == y.rate_n;
        for (size_t j = 0; j < 3; ++j) rate = rate && eq(x.rate_x[j], y.rate_x[j]) && eq(x.rate_y[j], y.rate_y[j]);
        if (!rate) fail(m, "rate table +0x8C");
    }
    if (a.offset_feel != b.offset_feel) fail(-1, "offset switch 0x622173");
    return ok;
}

void steer_force(ex::Car& c, sim::Extra& x, const WheelLayout& l, const ForceTables& t, const SteerTables& g, const ForceRace& r) {
    ForceExtra& o = x.force;
    o.wheel = {};
    const u8 kind = x.input.steer_kind;
    const int m = kind == 3 ? (x.input.wheel_ratio == 0 ? 1 : 2) : 0;
    const ForceMode& k = t.mode[size_t(m)];
    // A car a computer drives: the frame's steering.
    if (u8(u8(c.control) - 1) < 2 || c.driver_kind != 0) {
        s32 v = x.input.steer_in;
        if (kind == 3) v = v * 2;
        if (v < 0) v = v + 0x1F;
        v = v >> 5;
        s32 out = 0x80 - v;
        if (out < 0) out = 0;
        else if (!(out < 0x100)) out = 0xFF;
        o.out = u8(out);
        return;
    }
    const int n = l.count < 4 ? l.count : 4;
    auto ahead = [&](int i) { return s8(l.fore_aft[size_t(i)]) > 0; };
    // The front wheels' lateral forces over the front axle's load.
    F f = k0;
    for (int i = 0; i < n; ++i) {
        if (!ahead(i)) continue;
        const ex::Wheel& w = c.w[size_t(i)];
        F a = w.force_a;
        if (k.slip_on) {
            F s = c.t[size_t(i)].slip_angle;
            if (s < k0) s = -s;
            const F half = c.p.xe54 * kHalf;
            const F fx = w.fx_drive + w.fx_brake;
            const F rest = kOne - s * k.slip_inv;
            F share = k.slip_base + rest * k.slip_share;
            share = share + (fx / half) * k.drive_share;
            if (kTwo < share) share = kTwo;
            else if (share < kMinusOne) share = kMinusOne;
            a = a * share;
        }
        f = f + a;
    }
    f = f / c.p.xe54;
    clamp1(f);
    f = pedal_curve(k.curve[r.loose ? 1 : 0], f);
    const F strength = kind == 3 && r.strength >= 0 && r.strength < 3 ? k.strength[size_t(r.strength)] : kOne;
    const F gain = k.gain[r.loose ? 1 : 0];
    if (k.limit_kind == 1) {
        if (gain < f) f = gain;
        else if (f < -gain) f = -gain;
        f = f * strength;
    } else if (k.limit_kind == 2) {
        if (strength < f) f = strength;
        else if (f < -strength) f = -strength;
        f = f * gain;
    } else {
        f = f * gain;
        f = f * strength;
    }
    // The speed, and the steering offset's term.
    const F speed = gt3r::ee::sqrt(c.vel.x * c.vel.x + c.vel.y * c.vel.y);
    const bool slow = speed < kSlow;
    bool offset = false;
    if (g.mode != 0) offset = t.offset_feel == 2 || (t.offset_feel == 1 && slow);
    if (offset) {
        for (int i = 0; i < n; ++i) {
            if (!ahead(i)) continue;
            F a = k0;
            if (g.mode == 1) a = -x.tires.steer_offset[size_t(i)];
            else if (g.mode == 2) a = -(x.tires.steer_offset[size_t(i)] - c.w[size_t(i)].steer);
            const F y = pedal_curve(k.offset_curve, a / offset_limit(g, l.geometry[size_t(i)]));
            f = f + k.offset_gain * y;
            f = f - x.steer.offset_rate[size_t(i)] * k.offset_damp;
        }
        clamp1(f);
    }
    // The frame's steering change and the steering command.
    f = f + f_of((s32(x.input.steer_in) - s32(x.input.steer_in_last)) * 60) * k.turn_gain;
    F st = c.steer_cmd;
    if (k.steer_max < st) st = k.steer_max;
    else if (st < -k.steer_max) st = -k.steer_max;
    f = f + k.steer_gain * st;
    // The front wheels' aligning moment.
    F align = k0;
    for (int i = 0; i < n; ++i) {
        if (!ahead(i)) continue;
        const ex::Wheel& w = c.w[size_t(i)];
        const F cs = gt3r::ee::cos(w.steer), sn = gt3r::ee::sin(w.steer);
        const F d[3] = {cs * c.lat.x - sn * c.fwd.x, cs * c.lat.y - sn * c.fwd.y, cs * c.lat.z - sn * c.fwd.z};
        const ex::Vec3& gn = c.g[size_t(i)].normal;
        const F nv[3] = {gn.x, -gn.z, gn.y};
        F dot = k0;
        for (size_t j = 0; j < 3; ++j) dot = dot + d[j] * nv[j];
        align = align + dot * w.load;
    }
    F spring = c.p.susp[0].k + kSpring;
    spring = spring / kSpringRef;
    align = align * spring;
    f = f + (align * k.align_gain) / c.p.xe54;
    if (slow && !offset) {
        F s = c.steer_cmd;
        if (kSlowMax < s) s = kSlowMax;
        else if (s < -kSlowMax) s = -kSlowMax;
        f = s * kSlowGain;
    }
    if (kind == 3 && !r.loose) o.wheel = k.wheel;
    clamp1(f);
    // A kind 6 wheel: the steering change's factor where the force turns the same way.
    if (kind == 3 && r.turn_factor != 0) {
        s32 d = s32(x.input.steer_in) - s32(x.input.steer_in_last);
        if (r.lan_average) {
            const s32 sum = s32(x.input.steer_in) + s32(x.input.steer_seen_last);
            d = s32(x.input.steer_in) - ((sum + s32(u32(sum) >> 31)) >> 1);
        }
        F v = f_of(d);
        if (v < k0) v = -v;
        const F factor = rate_factor(k, v);
        if (d > 0) {
            if (k0 < f) f = f * factor;
        } else if (d < 0) {
            if (f < k0) f = f * factor;
        }
    }
    // The start's release: the force rises over 30 steps.
    const u8 left = x.start.release_count;
    if (left != 0) {
        x.start.release_count = u8(left - 1);
        f = f * (f_of(30 - s32(left)) * kThirtieth);
    }
    o.scaled = f * k127;
    o.out = u8(-0x80 - gt3r::ee::to_int(o.scaled));
}

} // namespace gt4r::sim::rules
