// Gran Turismo 4 Prologue's converter (convert_prologue.h). Each statement is one PlayStation 2 single-precision operation
// in the program's order; offsets are the block's (P), the engine block E = P + 0x138, the driveline D = P + 0x1F8, a tire
// record R = P + 0x398 + 0x1B8 r, an axle's suspension S = P + 0x2B8 + 0x44 a, a spec tire block B = spec + 0x142 + 0x70 k.
// Addresses are SCPS-15055 v1.01's (docs/formats/prologue.md "The converter").

#include "gt4r/sim/convert_prologue.h"

#include "base/ee_double.h"
#include "base/ee_math.h"
#include "gt4r/sim/exact_bridge.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/program_prologue.h"
#include "gt4r/sim/rules.h"
#include "sim/exact/build_math.h"

namespace gt4r::sim::prologue {

namespace {

using gt3r::ee::D;
using gt3r::ee::int_to_f;
using gt3r::ee::to_int;
using gt3r::s8;

// The program's literals (its compiler folded the decimal ones toward zero).
constexpr F k0 = F::raw(0x00000000), k1 = F::raw(0x3F800000), kHalf = F::raw(0x3F000000), kQuarter = F::raw(0x3E800000);
constexpr F k001 = F::raw(0x3C23D70A), kTenth = F::raw(0x3DCCCCCC), kMilli = F::raw(0x3A83126E);
constexpr F k5 = F::raw(0x40A00000), k10 = F::raw(0x41200000), k20 = F::raw(0x41A00000), k60 = F::raw(0x42700000);
constexpr F k100 = F::raw(0x42C80000), k200 = F::raw(0x43480000), k255 = F::raw(0x437F0000), k300 = F::raw(0x43960000);
constexpr F k10000 = F::raw(0x461C4000);
constexpr F kG = F::raw(0x411CCCCC);         // 9.8
constexpr F kDeg = F::raw(0x3C8EFA34);       // pi / 180
constexpr F kQuarterPi = F::raw(0x3F490FD9); // pi / 4
constexpr F kTwoPi = F::raw(0x40C90FDA);
constexpr F kRpm = F::raw(0x3DD6774E);    // 2 pi / 60
constexpr F kKmh = F::raw(0x3E8E38E3);    // 1 / 3.6
constexpr F kToKmh = F::raw(0x40666666);  // 3.6
constexpr F kTenKmh = F::raw(0x4031C71B); // 10 / 3.6
constexpr F kFifth = F::raw(0x3E4CCCCC), kTwentieth = F::raw(0x3D4CCCCC), kInch = F::raw(0x3CD013A9);
constexpr F k98 = F::raw(0x42C3FFFF), k980 = F::raw(0x4474FFFE), k9800 = F::raw(0x46191FFF), k7162 = F::raw(0x44330CCC);
constexpr F kAwd3 = F::raw(0x3EB851ED);                                    // 0.36
constexpr F kHalfPiRange = F::raw(0x3FC90FDA), kHalfPiB = F::raw(0x3FC90FD9); // curve A's range cap, curve B's span
constexpr F kCamberStep = F::raw(0x3D8EFA34), kThirtySecond = F::raw(0x3D000000);
constexpr F kHalfPi = F::raw(0x3FC90FDA), kPi = F::raw(0x40490FDA);

F cvt(s32 v) { return int_to_f(v); }

// The spec and the block as the program addresses them (little-endian images).
struct Img {
    u8* d;
    u8 b(u32 o) const { return d[o]; }
    s8 sb(u32 o) const { return s8(d[o]); }
    u16 h(u32 o) const { return u16(d[o] | (d[o + 1] << 8)); }
    s16 sh(u32 o) const { return s16(h(o)); }
    u32 w(u32 o) const { return u32(h(o)) | (u32(h(o + 2)) << 16); }
    F f(u32 o) const { return F::raw(w(o)); }
    void set8(u32 o, s32 v) { d[o] = u8(v); }
    void set16(u32 o, s32 v) { d[o] = u8(v), d[o + 1] = u8(u32(v) >> 8); }
    void set32(u32 o, u32 v) { set16(o, s32(v & 0xFFFF)), set16(o + 2, s32(v >> 16)); }
    void set(u32 o, F v) { set32(o, v.bits); }
};

struct Ctx {
    Img s, p;
    const ConvertData& d;
    const ConvertIn& in;
    u32 base;
    u32 addr(u32 off) const { return base + off; }
    u32 off(u32 a) const { return a - base; }
    u8 st(u32 o) const { return d.statics[o]; }
};

// 0x3C1AA8: a curve table {s16 n; u8 = 0, u8 = 0; F* x; F* y}.
void table_init(Ctx& c, u32 t, s32 n, u32 x, u32 y) {
    c.p.set16(t, n);
    c.p.set32(t + 4, c.addr(x));
    c.p.set32(t + 8, c.addr(y));
    c.p.set8(t + 2, 0);
    c.p.set8(t + 3, 0);
}

// 0x3C18A0: the curve at v, clamped at both ends; the segment by a walk from the second point.
template <class X, class Y> F lookup(s32 n, X x, Y y, F v) {
    if (v <= x(0)) return y(0);
    if (x(n - 1) <= v) return y(n - 1);
    const u32 last = u32(n - 1);
    u32 i = 1;
    while (i < last && !(v < x(s32(i)))) ++i;
    return y(s32(i) - 1) + ((v - x(s32(i) - 1)) * (y(s32(i)) - y(s32(i) - 1))) / (x(s32(i)) - x(s32(i) - 1));
}

// 0x3C15D8: the same with a bisection down to four points first (a tie with a point returns its value).
template <class X, class Y> F lookup_bisect(s32 n, X x, Y y, F v) {
    if (v <= x(0)) return y(0);
    if (x(n - 1) <= v) return y(n - 1);
    u32 lo = 0, hi = u32(n - 1);
    if (hi >= 4) {
        do {
            const u32 mid = (hi + lo) >> 1;
            if (v < x(s32(mid))) hi = mid;
            else if (v == x(s32(mid))) return y(s32(mid));
            else lo = mid;
        } while (hi - lo >= 4);
    }
    u32 i = lo;
    while (i < hi && !(v < x(s32(i)))) ++i;
    return y(s32(i) - 1) + ((y(s32(i)) - y(s32(i) - 1)) * (v - x(s32(i) - 1))) / (x(s32(i)) - x(s32(i) - 1));
}

F p_lookup(const Ctx& c, u32 t, F v, bool bisect = false) {
    const s32 n = c.p.sh(t);
    const u32 x = c.off(c.p.w(t + 4)), y = c.off(c.p.w(t + 8));
    auto X = [&](s32 i) { return c.p.f(x + 4 * u32(i)); };
    auto Y = [&](s32 i) { return c.p.f(y + 4 * u32(i)); };
    return bisect ? lookup_bisect(n, X, Y, v) : lookup(n, X, Y, v);
}

// 0x3C0D08: the wheel layout's row of a special car kind (spec +0x11): 4 → 1, 5 → 2, 6 → 3, else 0.
u8 layout_row(u8 kind) { return kind == 5 ? 2 : kind == 4 ? 1 : kind == 6 ? 3 : 0; }
// 0x3C0D80: the block's wheel layout (+0x30).
const std::array<u8, 0x54>& layout(const Ctx& c) { return c.d.layouts[c.p.b(0x30) & 3]; }
// 0x3C20C0: a special car whose body and tires come from the program's table (kinds 3 and 4).
bool special(const Img& s) { return u8(s.b(0x11) - 3) < 2; }
// 0x3C1F60: the table's row (0 for an index above 2).
const std::array<u8, 0x78>& special_row(const Ctx& c, u8 i) { return c.d.special[i < 3 ? i : 0]; }
F row_f(const std::array<u8, 0x78>& r, u32 o) {
    u32 v;
    std::memcpy(&v, &r[o], 4);
    return F::raw(v);
}

// ---- engine (E = P + 0x138) -------------------------------------------------------------------------------------------------

// 0x3B7BD8: stage `st`'s boost at rpm.
F stage_boost(const Ctx& c, u32 E, s32 rpm, u32 st) {
    const s32 knee = c.p.sh(E + 0x94 + 2 * st);
    if (!(rpm < knee)) {
        if (!(rpm < knee * 3)) return k0;
        const F d = cvt(rpm - knee);
        return c.p.f(E + 0xA4 + 4 * st) - ((c.p.f(E + 0xB4 + 4 * st) * d) * d) * kQuarter;
    }
    const u8 shape = c.p.b(E + 0x98 + st);
    if (shape == 1) {
        const F r = cvt(rpm);
        return (c.p.f(E + 0xB4 + 4 * st) * r) * r;
    }
    if (shape >= 2) return shape == 2 ? (c.p.f(E + 0xA4 + 4 * st) * cvt(rpm)) / cvt(knee) : k0;
    const F d = cvt(rpm - knee);
    return c.p.f(E + 0xA4 + 4 * st) - (c.p.f(E + 0xB4 + 4 * st) * d) * d;
}

// 0x3B7D08: 1 + the two stages' boost.
F boost(const Ctx& c, u32 E, s32 rpm0, s32 rpm1) {
    const s16 rpm[2] = {s16(rpm0), s16(rpm1)};
    F sum = k0;
    for (u32 st = 0; st < 2; ++st) {
        if (c.p.f(E + 0xA4 + 4 * st) == k0) continue;
        sum = sum + stage_boost(c, E, rpm[st], st);
    }
    return sum + k1;
}

// 0x3B7DC8: the boost, capped at 1 + E +0xBC.
F boost_capped(const Ctx& c, u32 E, s32 rpm0, s32 rpm1) {
    F b = boost(c, E, s16(rpm0), s16(rpm1));
    const F cap = c.p.f(E + 0xBC) + k1;
    if (cap < b) b = cap;
    return b;
}

// 0x3B7E20: the turbo stages from the spec's turbo bytes.
void turbo(Ctx& c, u32 E) {
    const Img& s = c.s;
    Img& p = c.p;
    const u8 cap = s.b(0xA7);
    if (cap == 0) p.set(E + 0xBC, k0);
    else {
        p.set(E + 0xBC, cvt(cap) * kTenth);
        if (s.b(0xAA) == 0 || s.b(0xAC) == 0 || s.b(0xAE) == 0) p.set(E + 0xBC, k0);
    }
    if (p.f(E + 0xBC) == k0) {
        p.set(E + 0xB8, k0), p.set16(E + 0x96, 0), p.set8(E + 0x98, 0), p.set(E + 0xA4, k0), p.set(E + 0xAC, k0), p.set(E + 0xB4, k0);
        p.set16(E + 0x94, 0), p.set8(E + 0x99, 0), p.set(E + 0xA8, k0), p.set(E + 0xB0, k0);
        return;
    }
    p.set8(E + 0x98, s.b(0xA8) < 3 ? s.b(0xA8) : 0);
    const F peak = cvt(s.b(0xAC)) * kTenth;
    p.set(E + 0xA4, peak);
    p.set(E + 0xAC, cvt(s.b(0xAE)) * kTenth);
    const s32 knee = s32(s.b(0xAA)) * 100;
    p.set(E + 0xB8, k0), p.set8(E + 0x99, 0), p.set(E + 0xA8, k0), p.set(E + 0xB0, k0), p.set16(E + 0x96, 0);
    p.set16(E + 0x94, knee);
    F kf = cvt(s16(knee));
    kf = kf * kf;
    p.set(E + 0xB4, peak / kf);
    const u8 a = s.b(0xAD), r = s.b(0xAF), k = s.b(0xAB);
    if (u32(a) + r + k == 0 || a == 0 || k == 0 || r == 0) return;
    p.set8(E + 0x99, s.b(0xA9) < 3 ? s.b(0xA9) : 0);
    const F peak1 = cvt(a) * kTenth;
    p.set(E + 0xA8, peak1);
    p.set(E + 0xB0, cvt(r) * kTenth);
    const s32 knee1 = s32(k) * 100;
    p.set16(E + 0x96, knee1);
    F k1f = cvt(s16(knee1));
    k1f = k1f * k1f;
    p.set(E + 0xB8, peak1 / k1f);
}

// 0x3B8008: idle and rev limit, the torque curve, its friction, the turbo and the boost cap's share of each point, the race's
// power cap (`cap` = the rule block's byte x 10, 0 = none).
void engine(Ctx& c, u32 E, s32 cap) {
    Img& s = c.s;
    Img& p = c.p;
    p.set16(E + 0x8E, s32(s.b(0x6E)) * 10);
    const u8 lim = s.b(0x6F);
    p.set16(E + 0x8C, lim ? s32(lim) * 100 : s32(s.b(0x71 + s.b(0x71))) * 100);
    const s32 floor = s32(s.b(0x70)) * 100 + 200;
    if (s32(p.h(E + 0x8C)) < floor) p.set16(E + 0x8C, floor);
    {
        const u32 n = s.b(0x71);
        if (s.sh(0x80 + 2 * n) == 0) s.set16(0x80 + 2 * n, s.h(0x7E + 2 * n));
    }
    const s32 n = s.b(0x71);
    for (s32 i = 0; i < n; ++i) p.set(E + 0xC + 4 * u32(i), cvt(s32(s.b(0x72 + u32(i))) * 100) * kRpm);
    const u8 lo_pct = s.b(0xA6);
    const F f_lo = lo_pct ? cvt(lo_pct) * k001 : k1;
    const F f_hi = s.h(0xA4) ? cvt(s.h(0xA4)) * kMilli : k1;
    const F w0 = p.f(E + 0xC), wn = p.f(E + 0xC + 4 * u32(n - 1));
    F scale = s.h(0xA2) ? cvt(s.h(0xA2)) * kMilli : k1;
    scale = scale * k001;
    if (n != 0) {
        const F span = f_hi - f_lo, range = wn - w0;
        for (s32 i = 0; i < n; ++i) {
            const F x = p.f(E + 0xC + 4 * u32(i)) - w0;
            s32 t = s.sh(0x82 + 2 * u32(i));
            if (t < -1) t += 0x10000;
            const F f = scale * (f_lo + (span * x) / range);
            p.set(E + 0x4C + 4 * u32(i), (cvt(t) * kG) * f);
        }
    }
    table_init(c, E, s.b(0x71), E + 0xC, E + 0x4C);
    const F friction = (cvt(s.h(0x6C)) * kTenth) * kG;
    p.set(E + 0x90, friction / p.f(E + 0xC + 4 * u32(s.b(0x71) - 1)));
    turbo(c, E);
    for (s32 i = 0; i < p.sh(E); ++i) {
        const s32 rpm = s16(s32(s.b(0x72 + u32(i))) * 100);
        F f = boost_capped(c, E, rpm, rpm);
        f = f / boost(c, E, rpm, rpm);
        p.set(E + 0x4C + 4 * u32(i), p.f(E + 0x4C + 4 * u32(i)) * f);
    }
    p.set8(E + 0x9B, 0);
    p.set(E + 0x9C, k0);
    if (cap <= 0) return;
    p.set8(E + 0x9B, 1);
    F w = ((cvt(cap) * kG) * k7162) * kTwoPi;
    const F soft = cvt(c.st(8)) * k001;
    w = w / k60;
    p.set(E + 0xA0, soft);
    p.set(E + 0x9C, w / (k1 - soft * kHalf));
}

// 0x3B9530 / 0x3B9710: the engine condition's factor (the limits from the static block's +0xA0 .. +0xA8) of the wear words
// e (+4) and o (+8).
F condition_factor(const Ctx& c, F e, F o) {
    auto t10k = [&](u32 k) { return cvt(s32(c.st(k)) * 10000); };
    const F t0 = -t10k(0xA0), t1 = cvt(c.st(0xA1)) * kMilli, t2 = t10k(0xA2), t3 = t10k(0xA3) + t2, t4 = cvt(c.st(0xA4)) * kMilli;
    const F t5 = -t10k(0xA7), t6 = cvt(c.st(0xA6)) * kMilli;
    F fe = k1;
    if (e < t0) fe = fe - t1;
    else if (e < k0) fe = fe - (e * t1) / t0;
    else if (!(e <= t2)) fe = e < t3 ? fe - ((e - t2) * t4) / (t3 - t2) : fe - t4;
    F fo = k1;
    if (o < t5) fo = t6 + fo;
    else if (o < k0) fo = (o * t6) / t5 + fo;
    return fe * fo;
}

// 0x3B7AC8: the full-throttle torque at engine speed w (the curve, bisected) x the condition's factor.
F torque_at(const Ctx& c, u32 E, F w) {
    const F t = p_lookup(c, E, w, true);
    return t * condition_factor(c, F::raw(c.in.condition[1]), F::raw(c.in.condition[2]));
}

// 0x3B7B40: the torque under the race's power cap (soft above (1 - share) x cap).
F capped_torque(const Ctx& c, u32 E, F t, F w) {
    const Img& p = c.p;
    if (p.b(E + 0x9B) == 0) return t;
    const F share = p.f(E + 0xA0);
    const F pw = t * w;
    const F cap = p.f(E + 0x9C);
    if (pw <= cap * (k1 - share)) return t;
    F lim = cap * (k1 - share * kHalf);
    if (!(cap <= pw)) {
        const F d = cap - pw;
        lim = lim - (d * d) / ((share + share) * cap);
    }
    lim = lim / w;
    return lim < t ? lim : t;
}

// ---- driveline (D = P + 0x1F8) ----------------------------------------------------------------------------------------------

// 0x3DFFD0: the layout D +0 from the drive type and four wheel drive system, the driven axle D +3, the coupling D +4 / D +8.
void layout(Ctx& c, u32 Dv) {
    const Img& s = c.s;
    Img& p = c.p;
    p.set(Dv + 4, k0), p.set(Dv + 8, k0);
    const u8 drive = s.b(0xB2);
    if (drive >= 5) return;
    if (drive == 0 || drive == 3 || drive == 4) return p.set8(Dv + 0, 0), p.set8(Dv + 3, 1);
    if (drive == 1) return p.set8(Dv + 0, 1), p.set8(Dv + 3, 0);
    p.set(Dv + 4, cvt(s.b(0xB4)) * k001);
    p.set8(Dv + 3, 1);
    const u8 sys = s.b(0xB3);
    auto cap10 = [&](s32 dflt) {
        const u8 b = s.b(0xB5);
        p.set(Dv + 8, cvt((b ? s32(b) : dflt) * 10) * kG);
    };
    switch (sys) {
    case 0: return p.set8(Dv + 0, 2), p.set8(Dv + 3, 0);
    case 1: return p.set8(Dv + 0, 3);
    case 2:
        p.set8(Dv + 0, 4);
        if (!(s.b(0xB4) < 50)) p.set(Dv + 4, k1);
        return cap10(8);
    case 3: {
        p.set8(Dv + 0, 5);
        const F k = p.f(Dv + 4) * kAwd3;
        p.set(Dv + 4, k);
        const F gain = cvt(c.d.awd3_gain) / k100;
        const F capacity = cvt(s32(c.d.awd3_cap) * 10) * kG;
        p.set(Dv + 4, k * gain);
        return p.set(Dv + 8, capacity);
    }
    case 4: {
        p.set8(Dv + 0, 6);
        const F k = p.f(Dv + 4);
        if (!(k == k0)) p.set(Dv + 4, k1 / ((k * k100) * kDeg));
        return cap10(15);
    }
    case 5: return p.set8(Dv + 0, 7); // (0x3BDA30: the system's settings into the program's words 0x714AFC ..)
    case 6: return p.set8(Dv + 0, 9), p.set8(Dv + 3, 0);
    case 7: return p.set8(Dv + 0, 8);
    case 8: return p.set8(Dv + 0, 10);
    default: return p.set8(Dv + 0, 0);
    }
}

// 0x3DF0D8: the differential type's index in the letters (an unknown character reads as itself).
s8 lsd_type(const Ctx& c, s8 ch) {
    for (s32 i = 0; i < 8; ++i)
        if (s8(c.d.lsd[size_t(i)]) == ch) return s8(i);
    return ch;
}

// 0x3DF248: the axles a layout drives (front, rear).
void driven(u8 lay, bool out[2]) {
    out[0] = out[1] = false;
    if (lay >= 11) return;
    if (lay == 0 || lay == 7) out[1] = true;
    else if (lay == 1) out[0] = true;
    else out[0] = out[1] = true;
}

// 0x3DFE98: an axle's tire radius, width and sidewall from the spec's size bytes (a width of 0 reads as 10 mm); a special
// car's radius and width from the program's table (+0 / +4 front, +8 / +0xC rear) with half the width for the sidewall.
F tire_size(const Ctx& c, u32 axle, F& radius, F& width, F& sidewall) {
    const Img& s = c.s;
    const u8 w = s.b(0x11A + axle);
    const s32 w5 = w ? s32(w) * 5 : 10;
    F rim = cvt(s.b(0x11C + axle)) * kInch;
    const F wf = cvt(w5) * kMilli;
    rim = rim * kHalf;
    F side = wf * cvt(s.b(0x11E + axle));
    width = wf;
    side = side * kTwentieth;
    radius = rim + side;
    sidewall = side;
    if (special(s)) {
        const auto& r = special_row(c, s.b(0x2D));
        width = row_f(r, axle ? 0xC : 4);
        radius = row_f(r, axle ? 8 : 0);
        sidewall = width * kHalf;
    }
    return radius;
}

// 0x3DF2A8: layout, differentials, gear ratios, tire sizes, inertias, launch and shift speeds, gear speed factors.
void driveline(Ctx& c, u32 Dv, u32 E) {
    Img& s = c.s;
    Img& p = c.p;
    layout(c, Dv);
    for (u32 i = 0; i < 2; ++i) p.set8(Dv + 0x10 + i, lsd_type(c, s.sb(0xD6 + i)));
    {
        bool on[2];
        driven(p.b(Dv), on);
        for (u32 a = 0; a < 2; ++a) {
            const u8 t = p.b(Dv + 0x10 + a);
            if (on[a]) {
                if (t == 0) p.set8(Dv + 0x10 + a, 2);
            } else if (t != 0) p.set8(Dv + 0x10 + a, 0);
        }
    }
    {
        // (the wheel layout of the spec's kind: a wheel whose byte +8 + i is 1 makes its axle's differential open)
        const auto& row = c.d.layouts[layout_row(s.b(0x11))];
        for (u32 i = 0; i < row[2]; ++i) {
            if (row[8 + i] != 1) continue;
            const u8 a = i < row[2] ? row[0x10 + i] : 0;
            if (p.b(Dv + 0x10 + a) != 0) p.set8(Dv + 0x10 + a, 1);
        }
    }
    for (u32 a = 0; a < 2; ++a) {
        const u8 t = p.b(Dv + 0x10 + a);
        if (t >= 8) continue;
        const u32 o = Dv + 4 * a;
        const F ini = cvt(s.b(0xD8 + a)), acc = cvt(s.b(0xDA + a)), dec = cvt(s.b(0xDC + a));
        switch (t) {
        case 0: case 1: case 2: p.set(o + 0x14, k0), p.set(o + 0x1C, k0), p.set(o + 0x24, k0); break;
        case 3: case 4: case 6: p.set(o + 0x14, ini * kG), p.set(o + 0x1C, acc * k001), p.set(o + 0x24, dec * k001); break;
        case 5: p.set(o + 0x14, ini * ini), p.set(o + 0x1C, (acc * kTenth) * kG), p.set(o + 0x24, k0); break;
        default: p.set(o + 0x14, ini * k001), p.set(o + 0x1C, acc * k001), p.set(o + 0x24, dec * k001); break;
        }
    }
    // (0x3DF1C8 generates the gear words when the spec's +0xCA and +0xCB are set; Prologue's builder leaves them 0)
    const u8 gears = s.b(0xC8);
    p.set8(Dv + 2, gears);
    const u8 kind = s.b(0xE1);
    p.set8(Dv + 0x12, kind);
    if (!(gears < 8)) p.set8(Dv + 2, 7);
    else if (kind == 3) {
        if (gears != 1) p.set8(Dv + 2, 1);
    } else if (kind == 1 && p.b(Dv + 2) < 2) p.set8(Dv + 0x12, 0);
    for (u32 g = 0; g <= p.b(Dv + 2); ++g) {
        const F r = cvt(s.sh(0xB6 + 2 * g)) * kMilli;
        const F fd = cvt(s.sh(0xC6)) * kMilli;
        p.set(Dv + 0x3C + 4 * g, r * fd);
    }
    p.set(Dv + 0x3C, -p.f(Dv + 0x3C));
    for (u32 a = 0; a < 2; ++a) {
        F r, w, sw;
        tire_size(c, a, r, w, sw);
        p.set(Dv + 0x7C + 4 * a, r), p.set(Dv + 0x8C + 4 * a, w), p.set(Dv + 0x94 + 4 * a, sw);
        if (!(p.f(Dv + 0x7C + 4 * a) == k0)) p.set(Dv + 0x84 + 4 * a, k1 / p.f(Dv + 0x7C + 4 * a));
    }
    p.set8(Dv + 1, 0);
    if (p.b(Dv) == 5 && !(p.f(Dv + 0x7C) == p.f(Dv + 0x80))) p.set8(Dv + 1, 1);
    F spin[2];
    for (u32 a = 0; a < 2; ++a) spin[a] = cvt(s.sh(0xD2 + 2 * a)) * k001;
    for (u32 a = 0; a < 2; ++a) {
        const F i = spin[a];
        if (i == k0) continue;
        p.set(Dv + 0xA0 + 4 * a, p.f(Dv + 0x7C + 4 * a) / i);
        const F inv_r = p.f(Dv + 0x84 + 4 * a);
        p.set(Dv + 0xB8 + 4 * a, (i * inv_r) * inv_r);
    }
    const F ie = cvt(s.h(0x6A)) * kMilli;
    p.set(Dv + 0xC, ie);
    if (s.h(0x6A) != 0) p.set(Dv + 0xB4, k1 / ie);
    for (u32 a = 0; a < 2; ++a) {
        F shaft = cvt(s.sh(0xCE + 2 * a)) * k001;
        if (p.b(Dv + 1) != 0 && a != p.b(Dv + 3)) shaft = shaft * (p.f(Dv + 0x7C + 4 * u32(p.b(Dv + 3))) / p.f(Dv + 0x7C + 4 * a));
        const F fd = cvt(s.sh(0xC6)) * kMilli;
        p.set(Dv + 0xA8 + 4 * a, (shaft * fd) + (spin[a] + spin[a]));
    }
    p.set(Dv + 0xB0, k1 / (p.f(Dv + 0xA8) + p.f(Dv + 0xAC)));
    p.set16(Dv + 0x2C, s32(s.b(0xC9)) * 10);
    p.set16(Dv + 0x2E, s32(s.b(0x70)) * 100);
    p.set(Dv + 0x9C, cvt(s.b(0xCD)) * kG);
    for (u32 g = 0; g <= p.b(Dv + 2); ++g)
        p.set(Dv + 0x5C + 4 * g, p.f(Dv + 0x3C + 4 * g) / p.f(Dv + 0x7C + 4 * u32(p.b(Dv + 3))));
    // (0x3DB658: the gearbox kinds 1 and 2, without steps: the launch speed is the idle speed)
    const bool stepless = u8(p.b(Dv + 0x12) - 1) < 2;
    const u16 idle = p.h(E + 0x8E);
    if (stepless) p.set16(Dv + 0x2C, idle);
    else if (s32(p.h(Dv + 0x2C)) - 500 < s32(idle)) p.set16(Dv + 0x2C, s32(idle) + 500);
    for (u32 g = 1; g < p.b(Dv + 2); ++g) {
        const u32 n = g + 1;
        const F up = ((cvt(p.h(Dv + 0x2E)) * kRpm) * p.f(Dv + 0x3C + 4 * n)) / p.f(Dv + 0x3C + 4 * g);
        s32 d = to_int((up * k60) / kTwoPi);
        const s32 launch = p.h(Dv + 0x2C);
        // (the static block's +0xDE: the first shift's speed halfway to the launch speed)
        if (c.st(0xDE) != 0 && g == 1) d = (launch + d) / 2;
        const s32 floor = launch + 500;
        const F v = p.f(Dv + 0x5C + 4 * n) * kTenKmh;
        d = d - to_int((v * k60) / kTwoPi);
        if (d < floor) d = floor;
        p.set16(Dv + 0x30 + 2 * (g - 1), d);
    }
    if (stepless && p.b(Dv + 2) >= 2)
        for (u32 g = 2; g <= p.b(Dv + 2); ++g) p.set(Dv + 0x3C + 4 * g, cvt(s.sh(0xB6 + 2 * g)) / cvt(s.sh(0xB8)));
    // 0x3CBA70: the up-shift speed's class (+0x13): 3 from 5,000 rpm, 2 from 4,500, else 1.
    const u16 up = p.h(Dv + 0x2E);
    p.set8(Dv + 0x13, up >= 5000 ? 3 : up >= 4500 ? 2 : 1);
}

// 0x427358: the driveline class: 1 FF and four wheel drive system 0, 3 system 3, 0 system 5, 2 the other systems, 0 the rest.
u8 driveline_class(u8 drive, u8 sys) {
    if (drive == 1) return 1;
    if (drive != 2) return 0;
    if (sys == 0) return 1;
    if (sys == 3) return 3;
    if (sys == 5) return 0;
    return 2;
}

// ---- tire records ------------------------------------------------------------------------------------------------------------

// 0x3C6F28: curves C (+0xD4) and D (+0x14C) on one slip ratio axis (+0xE0; the negative side reversed), the optimum slips
// +0x140 / +0x144 and C's braking side's greatest +0x148.
void slip_curves(Ctx& c, u32 R, u32 B) {
    Img& s = c.s;
    Img& p = c.p;
    s.set8(B + 0x14, 0);
    const s32 n1 = s.b(B + 0x13);
    s.set8(B + 0x20, s.b(B + 0x33));
    s.set8(B + 0x1A, 0), s.set8(B + 0x27, 0), s.set8(B + 0x2D, 0);
    const s32 n2 = s.b(B + 0x26);
    F best = k0;
    p.set(R + 0x140, k0);
    for (s32 i = 0; i < n1; ++i) {
        const u32 k = 4 * u32(n1 - (i + 1));
        const F x = (-cvt(s.b(B + 0x14 + u32(i)))) / k255;
        p.set(R + 0xE0 + k, x);
        const F cv = cvt(s.b(B + 0x1A + u32(i))) / k200;
        p.set(R + 0x110 + k, cv);
        if (best < cv) p.set(R + 0x140, x), best = cv;
        p.set(R + 0x158 + k, cvt(s.b(B + 0x20 + u32(i))) / k200);
    }
    best = k0;
    p.set(R + 0x144, k0);
    p.set(R + 0x148, k1);
    for (s32 i = 0; i < n2; ++i) {
        const u32 k = 4 * u32(n1 + i - 1);
        const F x = cvt(s.b(B + 0x27 + u32(i))) / k255;
        p.set(R + 0xE0 + k, x);
        const F cv = cvt(s.b(B + 0x2D + u32(i))) / k200;
        p.set(R + 0x110 + k, cv);
        if (best < cv) best = cv, p.set(R + 0x144, x), p.set(R + 0x148, best);
        p.set(R + 0x158 + k, cvt(s.b(B + 0x33 + u32(i))) / k200);
    }
    table_init(c, R + 0xD4, n1 + n2 - 1, R + 0xE0, R + 0x110);
    table_init(c, R + 0x14C, n1 + n2 - 1, R + 0xE0, R + 0x158);
}

// 0x3C7160: curve A (+0x48: the slip angle up to the block's range, at most pi / 2) and curve B (+0x98: up to pi / 2).
void angle_curves(Ctx& c, u32 R, u32 B) {
    Img& s = c.s;
    Img& p = c.p;
    const u8 deg = s.b(B + 1);
    s.set8(B + 3, 0);
    F range = cvt(deg ? deg : 0x5A) * kDeg;
    s.set8(B + 0xB, 0);
    if (kHalfPiRange < range) range = kHalfPiRange;
    const s32 n = s.b(B + 2);
    for (s32 i = 0; i < n; ++i) {
        p.set(R + 0x54 + 4 * u32(i), (cvt(s.b(B + 3 + u32(i))) * range) / k255);
        p.set(R + 0x74 + 4 * u32(i), cvt(s.b(B + 0xB + u32(i))) / k200);
    }
    table_init(c, R + 0x48, n, R + 0x54, R + 0x74);
    s.set8(B + 0x5F, 0);
    const s32 m = s.b(B + 0x5E);
    s.set8(B + 0x65, 0);
    for (s32 i = 0; i < m; ++i) {
        p.set(R + 0xA4 + 4 * u32(i), (cvt(s.b(B + 0x5F + u32(i))) * kHalfPiB) / k255);
        p.set(R + 0xBC + 4 * u32(i), cvt(s.b(B + 0x65 + u32(i))) / k200);
    }
    table_init(c, R + 0x98, m, R + 0xA4, R + 0xBC);
}

// 0x3BC218: the slip angle of curve A's peak: for a record read at its sideways speed (+0x1B5) the axis point of the curve's
// greatest value; else the whole degree 0 .. 45 where the curve (by 0x3BAF48's fold) peaks, 90 when it never rises above 0.
F peak_slip_angle(const Ctx& c, u32 R) {
    if (c.p.b(R + 0x1B5) != 0) {
        const u32 t = R + 0x48;
        const s32 n = c.p.sh(t);
        const u32 x = c.off(c.p.w(t + 4)), y = c.off(c.p.w(t + 8));
        F best = k0, at = k0;
        for (s32 i = 0; i < n; ++i)
            if (best < c.p.f(y + 4 * u32(i))) best = c.p.f(y + 4 * u32(i)), at = c.p.f(x + 4 * u32(i));
        return at;
    }
    F best = k0;
    s32 at = 0x5A;
    for (s32 d = 0; d < 0x2E; ++d) {
        F a = (cvt(d) * kDeg) - k0, sign = k1;
        if (a < -kHalfPi) a = a + kPi;
        else if (a < k0) sign = -k1, a = -a;
        else if (!(a < kHalfPi)) sign = -k1, a = kPi - a;
        const F v = p_lookup(c, R + 0x48, a) * sign;
        if (best < v) best = v, at = d;
    }
    return cvt(at) * kDeg;
}

// 0x3C7318: the base grip, each surface's grip (+0x08 + 4 s), the wear floor +0x34 (at most 1), the wear rate +0x04, the
// load curve (+0x188) and +0x00 = D(0) x 9.8.
void tire_grip(Ctx& c, u32 R, u32 B, F cap) {
    Img& s = c.s;
    Img& p = c.p;
    const u8 mu100 = s.b(B);
    const F mu = cvt(mu100 ? mu100 : 100) * k001;
    for (u32 i = 0; i < 16; ++i) {
        const u8 v = s.b(B + 0x4B + i);
        p.set(R + 8 + 4 * i, v ? (cvt(v) * k001) * mu : mu);
    }
    cap = cap / mu;
    p.set(R + 0x34, cap);
    p.set(R + 4, cvt(s.b(B + 0x6B)) * k001);
    if (k1 < cap) p.set(R + 0x34, k1);
    const s32 n = s.b(B + 0x39);
    s.set8(B + 0x3A, 0);
    for (s32 i = 0; i < n; ++i) {
        p.set(R + 0x194 + 4 * u32(i), (cvt(s.b(B + 0x3A + u32(i))) * k20) * kG);
        p.set(R + 0x1A4 + 4 * u32(i), cvt(s.b(B + 0x3E + u32(i))) / k200);
    }
    table_init(c, R + 0x188, n, R + 0x194, R + 0x1A4);
    p.set(R, p_lookup(c, R + 0x14C, k0, true) * kG);
}

// The base grip times its load curve at the axle's static load per wheel (0x3BF2C8: over the width of the wheel layout's
// axle's tire, +0x14 + the row's +0x0C + axle).
void grip_by_load(Ctx& c, u32 r) {
    Img& p = c.p;
    const u32 R = 0x398 + 0x1B8 * r, axle = r / 3;
    const auto& row = layout(c);
    const F load = (p.f(0xE6C + 4 * axle) * kHalf) + p.f(0x2F4 + 0x44 * axle);
    const u8 k = row[0xC + axle];
    const u8 w = k < row[1] ? row[0x14 + k] : 0;
    const F per_width = load / p.f(0x284 + 4 * u32(w));
    p.set(R, p.f(R) * p_lookup(c, R + 0x188, per_width));
}

// ---- suspension (S = P + 0x2B8 + 0x44 a) ------------------------------------------------------------------------------------

// 0x3C6A20: a damper's F(v) = o + g sqrt(v - t) through the two points (v1, f1), (v2, f2); all 0 when they do not fit.
void damper_fit(Ctx& c, u32 t, u32 o, u32 g, F v1, F f1, F v2, F f2) {
    Img& p = c.p;
    p.set(g, k0), p.set(o, k0), p.set(t, k0);
    if (f1 == k0 || f2 == k0 || v1 == k0 || v2 == k0 || v2 <= v1 || f2 <= f1) return;
    const F d = f1 * v2 - f2 * v1;
    if (d <= k0) return;
    const F e = ((f1 * f1) * v2) - ((f2 * f2) * v1);
    if (k0 < e) return;
    F q = e / (d + d);
    const F h = ((f1 * f2) * (f2 - f1)) / d;
    p.set(g, gt3r::ee::sqrt(h));
    q = q * q;
    const F tv = (-q) / h;
    p.set(t, tv);
    p.set(o, (-p.f(g)) * gt3r::ee::sqrt(-tv));
}

// 0x3C6B18: an axle's spring (x the motion ratio %), dampers, anti-roll, progressive gain, unsprung mass and bump damping.
void spring_block(Ctx& c, u32 S, u32 b, u8 ratio) {
    const Img& s = c.s;
    Img& p = c.p;
    const F r = cvt(ratio) * k001;
    p.set(S + 0x14, (cvt(s.b(b)) * k980) * r);
    const F scale = k100 / cvt(ratio);
    auto points = [&](u32 o, F& v1, F& f1, F& v2, F& f2) { // 0x3C6988
        f1 = (cvt(s.b(b + o + 1)) * k5) * kG;
        f2 = (cvt(s.b(b + o + 3)) * k5) * kG;
        v1 = (cvt(s.b(b + o)) * k001) * scale;
        v2 = (cvt(s.b(b + o + 2)) * k001) * scale;
    };
    F v1, f1, v2, f2;
    points(4, v1, f1, v2, f2);
    damper_fit(c, S + 0x18, S + 0x1C, S + 0x20, v1, f1, v2, f2);
    points(8, v1, f1, v2, f2);
    damper_fit(c, S + 0x24, S + 0x28, S + 0x2C, v1, f1, v2, f2);
    p.set(S + 0x30, (cvt(s.b(b + 1)) * k10) * kG);
    p.set(S + 0x34, (cvt(s.b(b + 2)) * k10000) * kG);
    p.set(S + 0x38, cvt(s.b(b + 3)) * kQuarter);
    p.set(S + 0x3C, (cvt(s.b(b + 3)) * kQuarter) * kG);
    p.set(S + 0x40, (cvt(s.b(b + 0xD)) * k10) * kG);
}

// 0x3C6D10: an axle's static place +0x10 (the axle's static load shared by its wheels in the layout, over the spring), the
// hub's offset +0x0C, the bump stop +0x04, the knee +0x00 and the droop limit +0x08.
void spring_static(Ctx& c, u32 S, u32 a) {
    const Img& s = c.s;
    Img& p = c.p;
    const F k = p.f(S + 0x14);
    if (!(k == k0)) {
        const F share = k1 / cvt(layout(c)[4 + a]);
        p.set(S + 0x10, ((-p.f(0xE6C + 4 * a)) * share) / k);
    }
    const F ride = p.f(0x40 + 4 * a);
    const F x = p.f(S + 0x10);
    p.set(S + 0xC, (p.f(0x274 + 4 * a) - ride) + x);
    F room = cvt(s.b(0x64 + a)) * kMilli;
    const F top = x - ride;
    if (ride < room) room = ride;
    const F limit = top + room;
    p.set(S + 4, limit);
    const F knee = cvt(s.b(0x46 + 16 * a)) * kMilli;
    if (ride < knee + room) p.set(S + 0, limit);
    else p.set(S + 0, (top + ride) - knee);
    const F pre = p.f(S + 0x3C) / p.f(S + 0x14);
    const F droop = x + cvt(s.b(0x66 + a)) * kMilli;
    p.set(S + 8, droop);
    if (pre < droop) p.set(S + 8, pre);
}

// ---- top speed --------------------------------------------------------------------------------------------------------------

// 0x3C6618: the top speed P +0xE10: from 0 to its first estimate in whole km/h steps (estimate / 500 + 1), shifting up past
// the up-shift speed, the last speed where the drive force beats the rolling resistance (with the downforce on the load) and
// the drag; a gearbox of kind 3 at 0x4247FFFF (50 m/s).
void top_speed(Ctx& c) {
    Img& p = c.p;
    const u32 E = 0x138, Dv = 0x1F8;
    if (p.b(Dv + 0x12) == 3) return p.set(0xE10, F::raw(0x4247FFFF));
    const s32 kmh = to_int(p.f(0xE10) * kToKmh);
    const s32 step = kmh / 500 + 1;
    s32 gear = 1;
    const bool cvt1 = p.b(Dv + 0x12) == 1;
    auto rx = [&](s32 i) { return c.d.roll_x[size_t(i)]; };
    auto ry = [&](s32 i) { return c.d.roll_y[size_t(i)]; };
    for (s32 k = 0; k < kmh; k += step) {
        const F v = cvt(k) * kKmh;
        const u32 g = 0x254 + 4 * u32(gear);
        F w = v * p.f(g);
        const F v2 = v * v;
        // 0x3DB670: a gearbox of kind 1 holds the engine at its hold speed (D +0x30): above it the ratio falls, down to the
        // last gear's.
        F ratio = k1;
        if (cvt1) {
            const F hold = cvt(p.h(Dv + 0x30)) * kRpm;
            if (!(w < hold)) {
                ratio = hold / w;
                const F last = p.f(Dv + 0x3C + 4 * u32(p.b(Dv + 2)));
                if (ratio < last) ratio = last;
            }
            w = ratio * w;
        }
        const s32 rpm = to_int((w * k60) / kTwoPi);
        const F t = capped_torque(c, E, torque_at(c, E, w), w);
        F drive = t * p.f(g);
        if (cvt1) drive = drive * ratio;
        const F lift_f = p.f(0xE7C) * v2, lift_r = p.f(0xE80) * v2;
        const F roll = lookup(c.d.roll_n, rx, ry, v) * c.d.roll_scale;
        const F resist = roll * ((p.f(0xE68) + lift_f) + lift_r) + p.f(0xE78) * v2;
        if (resist < drive) p.set(0xE10, v);
        if (!cvt1 && gear < s32(p.b(Dv + 2))) gear += s32(p.h(Dv + 0x2E)) < rpm;
    }
}

// 0x3B8478's power peak: the speed (rpm) of the engine's greatest power, in 100 rpm steps from the curve's first point.
s32 power_peak_rpm(const Ctx& c) {
    const Img& p = c.p;
    const u32 E = 0x138;
    std::array<s16, 16> pts{};
    s32 m = 0;
    for (s32 i = 0; i < p.sh(E) && i < s32(pts.size()); ++i) {
        const s32 rpm = to_int((p.f(E + 0xC + 4 * u32(i)) * k60) / kTwoPi);
        const s32 r = (rpm + 50) / 100 * 100;
        if (m <= 0 || pts[size_t(m - 1)] < r) pts[size_t(m++)] = s16(r);
    }
    if (m == 0) return 0;
    const s32 rev = p.h(E + 0x8C);
    const s32 top = std::max<s32>(rev, pts[size_t(m - 1)]);
    F best = k0;
    s32 at = 0;
    for (s32 rpm = pts[0]; rpm <= top; rpm += 100) {
        const F n = cvt(rpm);
        const F w = n * kRpm;
        const F kgfm = capped_torque(c, E, torque_at(c, E, w), w) / kG;
        const F ps = (kgfm * n) / k7162;
        if (rev < rpm) continue;
        if (best < ps) best = ps, at = rpm;
    }
    return at;
}

// 0x3C6868: the top speed's start (the rev limit in top gear), the braking constant P +0xE4C (the mass over twice the front
// brakes' torque over the radius times the front wheels), a gearbox of kind 1's hold speed (its peak power's speed, at
// most 500 rpm below the up-shift speed), then the walk.
void top_speed_start(Ctx& c) {
    Img& p = c.p;
    p.set(0xE10, (cvt(p.h(0x1C4)) * kRpm) / p.f(0x254 + 4 * u32(p.b(0x1FA))));
    F brake = (p.f(0xE40) * p.f(0x27C)) * cvt(layout(c)[4]);
    brake = brake + brake;
    p.set(0xE4C, p.f(0x48) / brake);
    if (p.b(0x1F8 + 0x12) == 1) {
        const s32 peak = power_peak_rpm(c), cap = s32(p.h(0x1F8 + 0x2E)) - 500;
        p.set16(0x1F8 + 0x30, cap < peak ? cap : peak);
    }
    top_speed(c);
}

} // namespace

ConvertData convert_data(const Program& program) {
    ConvertData d;
    const u32 st = PrologueProgram::kStatic;
    for (u32 k = 0; k < d.statics.size(); ++k) d.statics[k] = program.b(st + k);
    for (u32 r = 0; r < d.layouts.size(); ++r)
        for (u32 k = 0; k < d.layouts[r].size(); ++k) d.layouts[r][k] = program.b(PrologueProgram::kWheelLayouts + 0x54 * r + k);
    for (u32 r = 0; r < d.special.size(); ++r)
        for (u32 k = 0; k < d.special[r].size(); ++k) d.special[r][k] = program.b(PrologueProgram::kSpecialBodies + 0x78 * r + k);
    for (u32 k = 0; k < 8; ++k) d.lsd[k] = program.b(PrologueProgram::kLsdLetters + k);
    for (u32 k = 0; k < d.gear_steps.size(); ++k) d.gear_steps[k] = program.b(0x504AF0 + k);
    d.awd3_gain = program.b(PrologueProgram::kAwd3), d.awd3_cap = program.b(PrologueProgram::kAwd3 + 1);
    d.kind7_tire = program.b(PrologueProgram::kKind7Tire);
    d.air_density = cvt(d.statics[0]) * kTenth;
    // (0x3C5BC0: the static block's +0x0B points, x +0x0C + k (x 2 km/h) / 3.6, y +0x14 + k x 0.001)
    d.roll_n = d.statics[0x0B];
    for (u32 k = 0; k < 8 && k < d.roll_n; ++k) {
        d.roll_x[k] = cvt(s32(d.statics[0x0C + k]) << 1) * kKmh;
        d.roll_y[k] = cvt(d.statics[0x14 + k]) * kMilli;
    }
    d.roll_scale = cvt(s16(d.statics[0x2C] | (d.statics[0x2D] << 8))) * k001;
    for (u32 k = 0; k < 6; ++k) {
        d.steer_x[k] = program.b(0x571C78 + k), d.steer_y[k] = program.b(0x571C80 + k);
        d.curve_b_x[k] = program.b(0x503E18 + k), d.curve_b_y[k] = program.b(0x503E20 + k);
    }
    for (u32 k = 0; k < 16; ++k) d.surface_class[k] = program.b(0x571C88 + k);
    return d;
}

void core_spec(const Spec& s, const std::string& label, int course_kind, const ConvertData& d, ex::Spec& out) {
    sim::core_spec(s, out);
    auto h = [&](u32 o) { return s16(out[o] | (out[o + 1] << 8)); };
    auto set_h = [&](u32 o, s32 v) { out[o] = u8(v), out[o + 1] = u8(u32(v) >> 8); };
    // The inner builder (0x404620): no byte +0x0F, the pitch and roll inertia bytes as the chassis row has them, the
    // gearbox's kind (+0xE1), no controller bytes +0x112 / +0x113, the tire blocks' surfaces 10, 11 and 15 empty and their kind
    // bytes +0x6C .. +0x6E.
    out[0x0F] = 0;
    out[0x2F] = s.b(0x25), out[0x30] = s.b(0x26);
    out[0xE1] = s.b(0x158);
    out[0x112] = 0, out[0x113] = 0;
    for (int k = 0; k < 6; ++k) {
        const u32 B = (k < 3 ? 0x142u : 0x292u) + 0x70u * u32(k % 3);
        for (u32 row : {10u, 11u, 15u}) out[B + 0x4B + row] = 0;
        for (u32 j = 0; j < 3; ++j) out[B + 0x6C + j] = s.b(Spec::tire_block(k) + 3 + j);
    }
    // 0x3B3998.
    out[0x10] = 2;
    out[0x2D] = 0;
    if (label == "sl500_02" || label == "sl55_amg_02") out[0x11] = 7;
    if (label == "pod_tmv_01") out[0x11] = 1;
    if (label == "r35_gtr_tmv_01") out[0x11] = 2;
    if (label == "mtr_gmv_04") {
        out[0x11] = 8, out[0x35] = 0x26;
        if (course_kind == 2) {
            out[0x5C] = u8(out[0x5C] + 100), out[0x5D] = u8(out[0x5D] + 100);
            out[0x3A] = 0x3C, out[0x4A] = 0x32, out[0x67] = 100, out[0x66] = 100;
        }
    }
    out[0x0A] = 0;
    // The aerodynamic devices (spec +0x0A .. +0x0E): the kind, two speeds (km/h), a rate, a downforce.
    auto device = [&](u8 kind, u8 lo, u8 hi, u8 rate) { out[0x0A] = kind, out[0x0B] = lo, out[0x0C] = hi, out[0x0D] = rate, out[0x0E] = 0; };
    if (label == "r35_gtr_tmv_01") device(1, 0x3C, 0x50, 0x14);
    if (label == "crossfire_dmv_02" || label == "crossfire_04") device(1, 0x68, 0x70, 0x14);
    if (label == "cien_dmv_02") device(2, 0x38, 0x50, 5);
    if (label == "w12_tmv_01") device(1, 0x50, 0x78, 0x14);
    if (label == "hsr2_concept_89") device(4, 1, 0x50, 0xA);
    // The overhangs: half the length when 0.
    if (h(0x1C) == 0) set_h(0x1C, h(0x1A) / 2);
    if (h(0x1E) == 0) set_h(0x1E, h(0x1A) / 2);
    // The pitch inertia by the static block's signed byte +9 (percent more).
    if (const s32 more = s8(d.statics[9]); more != 0) {
        const s32 pct = more + 100 > 0 ? more + 100 : 100;
        out[0x2F] = u8(s32(out[0x2F]) * pct / 100);
    }
    // The second and third steering curves' defaults.
    if (out[0xFC] == 0) {
        out[0xFC] = 6;
        for (u32 k = 0; k < 6; ++k) out[0xFD + k] = d.steer_x[k], out[0x103 + k] = d.steer_y[k];
    }
    if (out[0xF5] == 0) {
        out[0xEF] = 6;
        for (u32 k = 0; k < 6; ++k) out[0xF0 + k] = d.steer_x[k], out[0xF6 + k] = d.steer_y[k];
    }
    if (out[0x112] == 0) out[0x112] = 100;
    if (out[0x113] == 0) out[0x113] = 50;
    if (out[0x117] == 100) out[0x110] = 0;
    out[0x12] = 1;
    for (u32 k = 0; k < 16; ++k) out[0x132 + k] = d.surface_class[k];
    // The front tire block's surfaces 12, 13, 14 from 0, 1, 9 when empty; each block's curve range (45 degrees) and curve B
    // when empty, and its empty surfaces from the front block's.
    {
        const u32 B = 0x142;
        if (out[B + 0x57] == 0) out[B + 0x57] = out[B + 0x4B];
        if (out[B + 0x58] == 0) out[B + 0x58] = out[B + 0x4C];
        if (out[B + 0x59] == 0) out[B + 0x59] = out[B + 0x54];
    }
    for (u32 k = 0; k < 6; ++k) {
        const u32 B = 0x142 + 0x70 * k;
        if (out[B + 1] == 0) out[B + 1] = 0x2D;
        if (out[B + 0x5E] == 0) {
            out[B + 0x5E] = 6;
            for (u32 j = 0; j < 6; ++j) out[B + 0x5F + j] = d.curve_b_x[j], out[B + 0x65 + j] = d.curve_b_y[j];
        }
        for (u32 i = 0; i < 16; ++i)
            if (out[B + 0x4B + i] == 0) out[B + 0x4B + i] = out[0x18D + i];
    }
}

void gear_ratios(const ex::Spec& spec, const ConvertData& d, int slider10, gt3r::s16 out[8]) {
    ex::Spec copy = spec;
    std::array<u8, 0x10> drive{};
    const ConvertIn in{};
    Ctx c{Img{copy.data()}, Img{drive.data()}, d, in, 0};
    layout(c, 0); // (only the driven axle, D +3, is read)
    F radius, width, side;
    tire_size(c, c.p.b(3), radius, width, side);
    const int n = c.s.b(0xC8);
    const u32 row = 6 * u32((n < 5 ? 5 : n) - 5);
    auto step = [&](u32 k) { return k200 / cvt(row + k < d.gear_steps.size() ? d.gear_steps[row + k] : 0); };
    const F t = cvt(slider10 - 150) / F::raw(0x437A0000);
    const gt3r::sim::Behaviour h = rules::behaviour();
    const F final_drive = ex::narrow(h, ex::mul(h, ex::widen(h, cvt(c.s.sh(0xC6))), D::raw(0x3F50624DD2F1A9FBull))); // (0x572CB8: 0.001, a unit low)
    F r = cvt(100 * s32(c.s.b(0x6F))) * kRpm;
    r = r * radius;
    r = r / (cvt(slider10) * kKmh);
    r = r / final_drive;
    auto eased = [&](u32 a, u32 b) {
        const F lo = step(b);
        return lo + t * (step(a) - lo);
    };
    F q1 = eased(0, 1), q2 = eased(2, 3);
    const F q3 = eased(4, 5);
    for (int i = n; i > 0; --i) {
        if (i == 1) r = r * q3;
        const F v = r * F::raw(0x447A0000);
        r = r * q1, q1 = q1 * q2;
        if (i < 8) out[i] = gt3r::s16(u16(u32(to_int(v)))); // (the low 16 bits)
    }
    const int rev = 3 * int(out[1]) + int(out[2]);
    out[0] = gt3r::s16(rev / 4); // (toward zero)
}

void convert(ex::Spec& spec, const ConvertData& d, const ConvertIn& in, u32 base, Block& block) {
    Ctx c{Img{spec.data()}, Img{block.data()}, d, in, base};
    Img& s = c.s;
    Img& p = c.p;
    p.set8(0x33, s.b(0x2D));
    if (s.b(0x2D) == 0 && !(s.b(0x142) < 0xB4)) p.set8(0x33, 1);
    p.set8(0x32, s.b(0x11));
    p.set8(0x30, layout_row(s.b(0x11)));
    if (!(s.b(0xB2) < 5)) s.set8(0xB2, 0);
    // Chassis: extents, wheelbase (a special car's from the table, +0x74), the distances to the center of gravity, tracks
    // (1 - wf and the mean track in the soft doubles).
    const F wf = cvt(s.b(0x2C)) * k001;
    p.set(0x00, cvt(s.sh(0x1C)) * kMilli);
    p.set(0x04, cvt(s.sh(0x1E)) * kMilli);
    p.set(0x18, cvt(s.sh(0x24)) * kMilli);
    if (special(s)) p.set(0x18, row_f(special_row(c, p.b(0x33)), 0x74));
    const F L = p.f(0x18);
    p.set(0x24, wf);
    const F b = L * wf;
    const F a = L - b;
    p.set(0x14, b);
    p.set(0x10, a);
    // (the soft doubles 0x4B5AC0 .. 0x4B63C0: the later builds', rounding toward zero)
    const gt3r::sim::Behaviour h = rules::behaviour();
    const D one_less = ex::sub(h, D::raw(0x3FF0000000000000ull), ex::widen(h, wf));
    p.set(0x20, ex::narrow(h, one_less));
    p.set(0x54, (-(a - b)) * kHalf);
    if (special(s)) {
        const auto& r = special_row(c, p.b(0x33));
        const F front = row_f(r, 0x70) * wf;
        p.set(0x54, -(row_f(r, 0x14) * (k1 - wf) + front));
    }
    {
        const F half = p.f(0x54);
        p.set(0x00, p.f(0x00) - half);
        p.set(0x04, p.f(0x04) + half);
    }
    p.set(0x08, cvt(s.sh(0x20)) * kMilli);
    const F hf = (cvt(s32(s.sh(0x26)) - s32(s.sb(0x68)) * 2) * kMilli) * kHalf;
    p.set(0x28, hf);
    const F hr = (cvt(s32(s.sh(0x28)) - s32(s.sb(0x69)) * 2) * kMilli) * kHalf;
    p.set(0x2C, hr);
    {
        const D front = ex::widen(h, hf * wf), back = ex::widen(h, hr);
        const D mean = ex::add(h, front, ex::mul(h, back, one_less));
        p.set(0x1C, ex::narrow(h, ex::add(h, mean, mean)));
    }
    p.set(0x0C, cvt(s.sh(0x22)) * kMilli);
    engine(c, 0x138, s32(in.rules[3]) * 10);
    driveline(c, 0x1F8, 0x138);
    p.set8(0x31, driveline_class(s.b(0xB2), s.b(0xB3)));
    // Tire records: surface s uses front record c_s and rear c_s + 3 (classes from 3 read as 0); only used records are built.
    s8 used[6] = {};
    for (u32 i = 0; i < 16; ++i) {
        u8 cls = s.b(0x132 + i);
        if (!(cls < 3)) cls = 0;
        p.set8(0xDE8 + i, cls);
        p.set8(0xDF8 + i, cls + 3);
        ++used[cls];
        ++used[cls + 3];
    }
    const bool late_grip = c.st(0xDA) != 0;
    for (u32 r = 0; r < 6; ++r) {
        if (used[r] == 0) continue;
        const u32 blk = s.b(0x12) ? r : r % 3;
        const u32 R = 0x398 + 0x1B8 * r, B = 0x142 + 0x70 * blk;
        for (u32 k = 0; k < 3; ++k) p.set8(R + 0x1B4 + k, s.b(B + 0x6C + k));
        slip_curves(c, R, B);
        angle_curves(c, R, B);
        const F peak = peak_slip_angle(c, R);
        p.set(R + 0x94, kQuarterPi < peak ? kQuarterPi : peak);
        tire_grip(c, R, B, cvt(c.st(0x8F + blk % 3)) * k001);
        // (the static block's +0xDA 0: the base grip by its load curve here, at the loads the block has so far)
        if (!late_grip) grip_by_load(c, r);
    }
    // Camber curves by axle: x = (128 - code) x 4 degrees / 32, y = code / 200, stored in reverse.
    for (u32 a = 0; a < 2; ++a) {
        const s32 n = s.b(0x120 + a);
        for (s32 i = 0; i < n; ++i) {
            const u32 k = 4 * u32(n - (i + 1)) + 16 * a;
            p.set(0x358 + k, (cvt(0x80 - s32(s.b(0x122 + 4 * a + u32(i)))) * kCamberStep) * kThirtySecond);
            p.set(0x378 + k, cvt(s.b(0x12A + 4 * a + u32(i))) / k200);
        }
        table_init(c, 0x340 + 12 * a, n, 0x358 + 16 * a, 0x378 + 16 * a);
    }
    // The tires' load knees by axle (static bytes x 98 N) over the width.
    for (u32 a = 0; a < 2; ++a) {
        const F lo = cvt(c.st(0x8C)) * k98, hi = cvt(c.st(0x8D)) * k98;
        const F dd = hi - lo;
        p.set(0xE98 + 4 * a, lo - dd);
        p.set(0xEA0 + 4 * a, dd / (p.f(0x284 + 4 * a) * k9800));
    }
    // The axles' grips (the chassis bytes + 900) x 0.001; a computer's car x the rule block's percentages by drive class.
    u32 pct[2] = {s.b(0x36), s.b(0x37)};
    for (u32 a = 0; a < 2; ++a) p.set(0xE08 + 4 * a, cvt(s32(pct[a]) + 0x384) * kMilli);
    if (in.control == 2) {
        const u8 cls = p.b(0x31);
        pct[0] = in.rules[4 + cls], pct[1] = in.rules[8 + cls];
        for (u32 a = 0; a < 2; ++a) {
            if (pct[a] == 0) pct[a] = 100;
            p.set(0xE08 + 4 * a, p.f(0xE08 + 4 * a) * (cvt(s32(pct[a])) * k001));
        }
    }
    // Aerodynamics: the frontal area (spec +0x16 / 10,000 m^2, or the width x the height) x the air's density / 2, then the
    // drag and lift coefficients; the aerodynamic device (spec +0x0A .. +0x0E: its switch, two speeds, a rate, a downforce).
    F area;
    if (const s16 ar = s.sh(0x16); ar != 0) area = (cvt(ar) / k10000) * (d.air_density * kHalf);
    else area = ((p.f(0x08) * p.f(0x0C)) * d.air_density) * kHalf;
    const F cd = cvt(s.b(0x32)) * k001;
    p.set(0xE74, cd);
    p.set(0xE78, area * cd);
    p.set(0xE7C, area * (cvt(s.b(0x33)) * k001));
    p.set(0xE80, area * (cvt(s.b(0x34)) * k001));
    p.set8(0xE84, s.b(0x0A));
    if (s.b(0x0A) != 0) {
        p.set(0xE88, cvt(s.b(0x0B)) * kKmh);
        p.set(0xE8C, cvt(s.b(0x0C)) * kKmh);
        p.set(0xE90, s.b(0x0D) == 0 ? k300 : k10 / cvt(s.b(0x0D)));
        p.set(0xE94, area * (cvt(s.b(0x0E)) * k001));
    } else {
        p.set32(0xE88, 0), p.set32(0xE8C, 0), p.set32(0xE90, 0), p.set32(0xE94, 0);
    }
    p.set(0xE78, p.f(0xE78) + (p.f(0xE7C) + p.f(0xE80)) * kTenth);
    // Brakes: the torque by wheel (bytes x 98 N m), the radius over the torque, the handbrake, the controller's settings.
    for (u32 a = 0; a < 2; ++a) {
        const u8 t = s.b(0xDE + a);
        p.set(0xE40 + 4 * a, cvt(s32(t) * 10) * kG);
        p.set(0xE50 + 4 * a, cvt(s.b(0x10A + a)) * kTenth);
        p.set(0xE58 + 4 * a, cvt(s.b(0x112 + a)) * k001);
        if (t == 0) p.set32(0xE60 + 4 * a, 0);
        else p.set(0xE60 + 4 * a, p.f(0x274 + 4 * a) / p.f(0xE40 + 4 * a));
    }
    p.set(0xE48, cvt(s32(s.b(0xE0)) * 10) * kG);
    // The aids (A = P + 0xEA8): the stability control's channels and switches, the traction control.
    {
        const u32 A = 0xEA8;
        const u8 fl = s.b(0x109);
        p.set(A + 0x00, cvt(s.b(0x10C)) * k001);
        p.set(A + 0x04, cvt(s.b(0x10D)) * k001);
        p.set(A + 0x08, cvt(s.b(0x111)) * k001);
        p.set8(A + 0x0C, fl & 1), p.set8(A + 0x0D, (fl >> 1) & 1), p.set8(A + 0x0E, (fl >> 2) & 1), p.set8(A + 0x0F, (fl >> 3) & 1);
        u32 word = p.w(A + 0x0C);
        p.set8(A + 0x30, (word & 0xFFFF) != 0 && !(p.f(A + 0x00) == k0));
        p.set8(A + 0x31, (word & 0xFFFF0000u) != 0 && !(p.f(A + 0x08) == k0));
        const F c_gain = cvt(s.b(0x114)) * k001;
        p.set(A + 0x10, c_gain);
        p.set(A + 0x14, cvt(s.b(0x115)) * k001);
        p.set8(A + 0x1C, (fl >> 4) & 1), p.set8(A + 0x1D, (fl >> 5) & 1), p.set8(A + 0x1E, (fl >> 6) & 1), p.set8(A + 0x1F, fl >> 7);
        p.set(A + 0x18, cvt(s.b(0x116)) * k001);
        word = p.w(A + 0x1C);
        p.set8(A + 0x32, (word & 0xFFFF) != 0 && !(c_gain == k0));
        p.set8(A + 0x33, (word & 0xFFFF0000u) != 0 && !(p.f(A + 0x18) == k0));
        p.set(A + 0x24, cvt(s.b(0x10E)) * kTenth);
        p.set(A + 0x28, cvt(s.b(0x10F)) * k001);
        p.set(A + 0x20, cvt(s.b(0x110)) * kTenth);
        const u8 lvl = s.b(0x117);
        p.set(A + 0x2C, lvl == 100 || lvl == 0 ? k1 : cvt(lvl) * k001);
    }
    // Steering: the full lock, the three speed curves (0x3C6560: x = byte / 3.6 m/s, y = byte x 0.2 degree), the Ackermann
    // ratios, the steer-coupled camber (a special car's angle from the table, +0x30).
    p.set(0x58, cvt(s.b(0x35)) * kDeg);
    auto steer_curve = [&](u32 t, u32 x, u32 y, u32 n, u32 sx, u32 sy) {
        for (u32 i = 0; i < n; ++i) {
            p.set(x + 4 * i, cvt(s.b(sx + i)) * kKmh);
            p.set(y + 4 * i, (cvt(s.b(sy + i)) * kFifth) * kDeg);
        }
        table_init(c, t, s32(n), x, y);
    };
    steer_curve(0x5C, 0x68, 0x80, s.b(0xE2), 0xE3, 0xE9);
    steer_curve(0x98, 0xA4, 0xBC, s.b(0xFC), 0xFD, 0x103);
    steer_curve(0xD4, 0xE0, 0xF8, s.b(0xEF), 0xF0, 0xF6);
    // (0x42A850: the later builds' atan2)
    {
        const F wb = p.f(0x18), ht = p.f(0x28);
        p.set(0x110, ex::atan2(h, wb, wb - ht) / kQuarterPi);
        p.set(0x114, ex::atan2(h, wb, wb + ht) / kQuarterPi);
    }
    if (const u8 sc = s.b(0x18); sc == 0) p.set32(0xE34, 0);
    else {
        F angle = cvt(sc) * kDeg;
        if (special(s)) angle = row_f(special_row(c, p.b(0x33)), 0x30);
        p.set(0xE34, gt3r::ee::sin(angle));
    }
    // Toe and camber by axle: toe = atan2((byte - 128) x 0.0005, radius), the camber, its steering part, its curve's value at
    // the camber, the camber's grip share.
    for (u32 a = 0; a < 2; ++a) {
        const F toe = (cvt(s32(s.b(0x60 + a)) - 0x80) * kHalf) * kMilli;
        p.set(0xE38 + 4 * a, ex::atan2(h, toe, p.f(0x274 + 4 * a)));
        p.set(0xE14 + 4 * a, -((cvt(s.b(0x5A + a)) * kTenth) * kDeg));
        p.set(0xE1C + 4 * a, cvt(s.sb(0x62 + a)) * kDeg);
        p.set(0xE24 + 4 * a, p_lookup(c, 0x340 + 12 * a, p.f(0xE14 + 4 * a), true));
        p.set(0xE2C + 4 * a, cvt(s.b(0x38 + a)) * k001);
    }
    // Inertias: yaw (6,000 when 0), pitch (4,000) and roll (2,000), the levers.
    const u8 y = s.b(0x2E);
    const F yaw = y ? cvt(y) * k100 : F::raw(0x45BB8000);
    p.set(0x118, yaw);
    p.set(0x11C, k1 / yaw);
    const u8 pb = s.b(0x2F);
    const F pitch = pb ? cvt(pb) * k100 : F::raw(0x457A0000);
    p.set(0x120, (p.f(0x10) * kTwoPi) / pitch);
    p.set(0x124, (p.f(0x14) * kTwoPi) / pitch);
    const u8 ro = s.b(0x30);
    const F roll = ro ? cvt(ro) * k100 : F::raw(0x44FA0000);
    p.set(0x128, (p.f(0x28) * kTwoPi) / roll);
    p.set(0x12C, (p.f(0x2C) * kTwoPi) / roll);
    spring_block(c, 0x2B8, 0x3A, s.b(0x5E));
    spring_block(c, 0x2FC, 0x4A, s.b(0x5F));
    // Ride heights (each axle's own), the center of gravity's height and the load transfers.
    {
        const F rh = cvt(s.b(0x5C)) * kMilli;
        p.set(0x40, rh);
        const F rh2 = cvt(s.b(0x5D)) * kMilli;
        p.set(0x44, rh2);
        const F mid = rh * p.f(0x24) + rh2 * p.f(0x20);
        p.set(0x3C, mid);
        const F hh = ((cvt(s.b(0x31)) * kMilli) + mid) * kHalf;
        p.set(0x38, hh / p.f(0x18));
        p.set(0x34, hh / p.f(0x1C));
    }
    // The mass: the catalog weight and the program's added mass (its first byte alone for layout 7), its inverse and weight;
    // the sprung mass (less each wheel's unsprung mass by the wheel layout's wheels on each axle) and its loads by axle.
    if (const s16 kg = s.sh(0x2A); kg != 0) {
        F m = cvt(kg);
        p.set(0x48, m);
        if (p.b(0x1F8) == 7) m = m + cvt(c.st(3));
        else m = m + cvt(s32(c.st(3)) + c.st(4));
        p.set(0x48, m);
        m = p.f(0x48);
        const F inv = k1 / m;
        p.set(0xE68, m * kG);
        p.set(0x4C, inv);
        const auto& row = layout(c);
        F sprung;
        if (row[4] == 2 && row[5] == 2) {
            F u = p.f(0x2F0) + p.f(0x334);
            u = u + u;
            sprung = m - u;
        } else {
            const F rr = p.f(0x334) * cvt(row[5]);
            sprung = m - (p.f(0x2F0) * cvt(row[4]) + rr);
        }
        if (!(sprung <= k0)) {
            const F share = sprung / m;
            const F wsp = sprung * kG;
            p.set(0x34, sprung * p.f(0x34));
            p.set(0xE70, wsp * p.f(0x20));
            p.set(0x38, share * p.f(0x38));
            p.set(0xE6C, wsp * p.f(0x24));
            p.set(0x50, k1 / sprung);
        }
    }
    // The pitch's tire terms (the static byte +7 x 0.01 x 2 pi over the pitch inertia + m d^2; none for the special kind 7
    // while 0x503CB0 is 0).
    {
        const u8 t7 = s.b(0x11) == 7 && c.d.kind7_tire == 0 ? 0 : c.st(7);
        const F m = p.f(0x48), da = p.f(0x10), db = p.f(0x14);
        const F t = (cvt(t7) * k001) * kTwoPi;
        p.set(0x130, t / (pitch + (m * da) * da));
        p.set(0x134, t / (pitch + (m * db) * db));
    }
    // (the static block's +0xDA set: the base grip by its load curve now, at the axles' static loads)
    if (late_grip)
        for (u32 r = 0; r < 6; ++r)
            if (!(p.f(0x398 + 0x1B8 * r) == k0)) grip_by_load(c, r);
    for (u32 a = 0; a < 2; ++a) spring_static(c, 0x2B8 + 0x44 * a, a);
    top_speed_start(c);
}

u32 block_offset(u32 o) {
    if (o < 0x398) return o;
    if (o < 0xDD0) return 0x398 + 0x1B8 * ((o - 0x398) / 0x1B4) + (o - 0x398) % 0x1B4;
    return o + (o < 0xE6C ? 24 : 44);
}

ex::ParamImage engine_image(const Block& p, u32 base) {
    ex::ParamImage out{};
    for (u32 o = 0; o < out.size(); ++o) out[o] = p[block_offset(o)];
    // The curve tables' pointers, at the engine's offsets.
    auto table = [&](u32 t) {
        for (u32 k = 4; k <= 8; k += 4) {
            u32 v;
            std::memcpy(&v, &out[t + k], 4);
            if (v < base || v >= base + kBlockSize) continue;
            const u32 at = v - base;
            u32 back = at;
            for (u32 o = 0; o < out.size(); ++o)
                if (block_offset(o) == at) {
                    back = o;
                    break;
                }
            v = base + back;
            std::memcpy(&out[t + k], &v, 4);
        }
    };
    table(0x138);
    for (u32 t : {0x5Cu, 0x98u, 0xD4u, 0x340u, 0x34Cu}) table(t);
    for (u32 r = 0; r < 6; ++r)
        for (u32 t : {0x48u, 0x98u, 0xD4u, 0x14Cu, 0x188u}) table(0x398 + 0x1B4 * r + t);
    return out;
}

} // namespace gt4r::sim::prologue
