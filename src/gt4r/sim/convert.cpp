// Gran Turismo 4's converter (convert.h): 0x361908 and its routines, each named by its address, in the
// program's arithmetic and order. Offsets: P the parameter block, E = P + 0x134 the engine's block, D = P +
// 0x234 the driveline's, S = P + 0x308 + 0x58 x axle an axle's suspension, R = P + 0x410 + 0x1EC x record a
// tire record; s the spec.

#include "gt4r/sim/convert.h"

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/exact_bridge.h"
#include "base/ee_math.h"
#include "sim/exact/build_math.h"
#include "sim/exact/car_fields.h"

#include <vector>

#include <cmath>
#include <type_traits>

namespace gt4r::sim {

namespace {

using gt3r::ee::F;
using gt3r::ee::int_to_f;
using gt3r::ee::to_int;
using gt3r::s8;

const F kZero = F(), kOne = F::raw(0x3F800000), kHalf = F::raw(0x3F000000), kQuarter = F::raw(0x3E800000);
const F kMilli = F::raw(0x3A83126E), kCenti = F::raw(0x3C23D70A), kTenth = F::raw(0x3DCCCCCC), kG = F::raw(0x411CCCCC);
const F kHundred = F::raw(0x42C80000), kDegree = F::raw(0x3C8EFA34), kKmh = F::raw(0x3E8E38E3), kRpm = F::raw(0x3DD6774E);
const F kInch = F::raw(0x3CD013A9), k255 = F::raw(0x437F0000), k200 = F::raw(0x43480000), kSixty = F::raw(0x42700000);
const F kTwoPi = F::raw(0x40C90FDA);

constexpr u32 kE = 0x134, kD = 0x234;
constexpr u32 kSpecial = 0x6205C8, kDiffLetters = 0x620300, kAwdBytes = 0x620308, kKindTable = 0x622320;

// 0x359A38: a table object at `at`: the count (s16), two zero bytes, the pointers to its x and y values.
void table(ParamBlock& p, u32 at, u32 n, u32 x, u32 y) {
    p.set_h(at, n), p.set_b(at + 2, 0), p.set_b(at + 3, 0);
    p.set_w(at + 4, p.base + x), p.set_w(at + 8, p.base + y);
}
// A table's points read back through its pointers (offsets in the block).
u32 tx(const ParamBlock& p, u32 at) { return p.w(at + 4) - p.base; }
u32 ty(const ParamBlock& p, u32 at) { return p.w(at + 8) - p.base; }

// 0x359830: y(v), linear between the points, found by a scan.
F lookup_scan(const ParamBlock& p, u32 at, F v) {
    const u32 x = tx(p, at), y = ty(p, at);
    const s32 last = s32(p.sh(at)) - 1;
    if (v <= p.f(x)) return p.f(y);
    if (p.f(x + 4 * u32(last)) <= v) return p.f(y + 4 * u32(last));
    u32 k = 1;
    while (s32(k) < last && !(v < p.f(x + 4 * k))) ++k;
    const F x0 = p.f(x + 4 * k - 4), x1 = p.f(x + 4 * k), y0 = p.f(y + 4 * k - 4), y1 = p.f(y + 4 * k);
    return y0 + ((v - x0) * (y1 - y0)) / (x1 - x0);
}
// 0x359568: the same with a bisection over four points or more (an exact hit gives its y).
F lookup_bisect(const ParamBlock& p, u32 at, F v) {
    const u32 x = tx(p, at), y = ty(p, at);
    const u32 last = u32(s32(p.sh(at)) - 1);
    if (v <= p.f(x)) return p.f(y);
    if (p.f(x + 4 * last) <= v) return p.f(y + 4 * last);
    u32 hi = last, lo = 0;
    while (!(hi < 4) && hi - lo >= 4) {
        const u32 mid = (hi + lo) >> 1;
        const F m = p.f(x + 4 * mid);
        if (v < m) hi = mid;
        else if (v == m) return p.f(y + 4 * mid);
        else lo = mid;
    }
    u32 k = lo;
    if (k < hi && !(v < p.f(x + 4 * k))) {
        ++k;
        while (k < hi && !(v < p.f(x + 4 * k))) ++k;
    }
    const F x0 = p.f(x + 4 * k - 4), x1 = p.f(x + 4 * k), y0 = p.f(y + 4 * k - 4), y1 = p.f(y + 4 * k);
    return y0 + ((y1 - y0) * (v - x0)) / (x1 - x0);
}

// 0x359510: the wheel layout's row of the block (its byte +0x30).
u32 layout_row(const ParamBlock& p) { return rules::kWheelLayouts + rules::kWheelLayoutSize * p.b(0x30); }
// 0x359EF8: the special cars' row (0 past 2).
u32 special_row(u8 i) { return kSpecial + 0x78 * (i < 3 ? i : 0); }

// ---- the engine (0x34EF30, 0x34ED48, 0x34EB00 / 0x34EC30 / 0x34ECF0) ----

// 0x34EB00: a turbo stage's boost at rpm.
F stage_boost(const ParamBlock& p, s32 rpm, u32 st) {
    const s32 knee = p.sh(kE + 0xD4 + 2 * st);
    const F peak = p.f(kE + 0xE4 + 4 * st), coef = p.f(kE + 0xF4 + 4 * st);
    if (rpm < knee) {
        switch (p.b(kE + 0xD8 + st)) {
        case 0: {
            const F d = int_to_f(rpm - knee);
            return peak - (coef * d) * d;
        }
        case 1: return (coef * int_to_f(rpm)) * int_to_f(rpm);
        case 2: return (peak * int_to_f(rpm)) / int_to_f(knee);
        default: return kZero;
        }
    }
    if (rpm < knee * 2 + knee) {
        const F d = int_to_f(rpm - knee);
        return peak - ((coef * d) * d) * kQuarter;
    }
    return kZero;
}
// 0x34EC30: 1 + both stages' boost.
F boost_raw(const ParamBlock& p, s32 rpm) {
    F sum = kZero;
    for (u32 st = 0; st < 2; ++st)
        if (!(p.f(kE + 0xE4 + 4 * st) == kZero)) sum = sum + stage_boost(p, rpm, st);
    return sum + kOne;
}
// 0x34ECF0: the same, capped at 1 + the boost cap.
F boost_capped(const ParamBlock& p, s32 rpm) {
    F b = boost_raw(p, rpm);
    const F cap = p.f(kE + 0xFC) + kOne;
    if (cap < b) b = cap;
    return b;
}

void turbo(const Spec& s, ParamBlock& p) {
    p.set_w(kE + 0xFC, 0);
    if (s.b(0x113) != 0) {
        p.set_f(kE + 0xFC, int_to_f(s.b(0x113)) * kTenth);
        if (s.b(0x116) == 0 || s.b(0x118) == 0 || s.b(0x11A) == 0) p.set_w(kE + 0xFC, 0);
    }
    const F zero = kZero;
    if (p.f(kE + 0xFC) == zero) {
        p.set_f(kE + 0xF8, zero), p.set_h(kE + 0xD6, 0), p.set_b(kE + 0xD8, 0), p.set_f(kE + 0xE4, zero), p.set_f(kE + 0xEC, zero);
        p.set_f(kE + 0xF4, zero), p.set_h(kE + 0xD4, 0), p.set_b(kE + 0xD9, 0), p.set_f(kE + 0xE8, zero), p.set_f(kE + 0xF0, zero);
        return;
    }
    p.set_b(kE + 0xD8, s.b(0x114) < 3 ? s.b(0x114) : 0);
    const F peak = int_to_f(s.b(0x118)) * kTenth;
    p.set_f(kE + 0xE4, peak);
    p.set_f(kE + 0xEC, int_to_f(s.b(0x11A)) * kTenth);
    p.set_f(kE + 0xF8, zero), p.set_b(kE + 0xD9, 0), p.set_f(kE + 0xE8, zero), p.set_f(kE + 0xF0, zero), p.set_h(kE + 0xD6, 0);
    {
        const u32 v = u32(s.b(0x116)) * 25;
        p.set_h(kE + 0xD4, v * 4);
        const F k = int_to_f(s16(u16(v << 2)));
        p.set_f(kE + 0xF4, peak / (k * k));
    }
    const u8 a2 = s.b(0x119), a3 = s.b(0x11B), v1 = s.b(0x117);
    if (a2 + a3 + v1 == 0 || a2 == 0 || v1 == 0 || a3 == 0) return;
    p.set_b(kE + 0xD9, s.b(0x115) < 3 ? s.b(0x115) : 0);
    const F peak2 = int_to_f(s.b(0x119)) * kTenth;
    p.set_f(kE + 0xE8, peak2);
    p.set_f(kE + 0xF0, int_to_f(s.b(0x11B)) * kTenth);
    const u32 v = u32(s.b(0x117)) * 25;
    p.set_h(kE + 0xD6, v * 4);
    const F k = int_to_f(s16(u16(v << 2)));
    p.set_f(kE + 0xF8, peak2 / (k * k));
}

void engine(Spec& s, const Program& program, ParamBlock& p, s32 power_rule) {
    p.set_h(kE + 0xCE, u32(s.b(0xBC)) * 10);
    if (s.b(0xBD) != 0) p.set_h(kE + 0xCC, u32(s.b(0xBD)) * 100);
    else p.set_h(kE + 0xCC, u32(s.b(0xBF + s.b(0xBF))) * 100);
    {
        const s32 least = s32(s.b(0xBE)) * 100 + 200;
        if (s32(p.h(kE + 0xCC)) < least) p.set_h(kE + 0xCC, u32(least));
    }
    u32 n = s.b(0xBF);
    // The last torque word, when 0, is the one before it (in the spec).
    if (s.h(0xD6 + 2 * n) == 0) s.set_h(0xD6 + 2 * n, s.h(0xD4 + 2 * n)), n = s.b(0xBF);
    for (u32 i = 0; i < n; ++i) p.set_f(kE + 0x0C + 4 * i, int_to_f(s32(s.b(0xC0 + i)) * 100) * kRpm);
    const F low = s.b(0x10C) ? int_to_f(s.b(0x10C)) * kCenti : kOne;
    F high = s.h(0x10A) ? int_to_f(s.h(0x10A)) * kMilli : kOne;
    const F x0 = p.f(kE + 0x0C), xn = p.f(kE + 0x08 + 4 * n);
    F out = s.h(0x108) ? int_to_f(s.h(0x108)) * kMilli : kOne;
    out = out * kCenti;
    if (const u8 pw = s.b(0x07); pw != 0 && pw != 100) out = out * (int_to_f(pw) / kHundred);
    if (n != 0) {
        high = high - low;
        const F span = xn - x0;
        for (u32 i = 0; i < n; ++i) {
            const F dx = p.f(kE + 0x0C + 4 * i) - x0;
            const s32 w = s.sh(0xD8 + 2 * i);
            const s32 t = w < -1 ? w + 0x10000 : w;
            F f = (high * dx) / span;
            f = low + f;
            f = out * f;
            p.set_f(kE + 0x6C + 4 * i, (int_to_f(t) * kG) * f);
        }
    }
    table(p, kE, s.b(0xBF), kE + 0x0C, kE + 0x6C);
    {
        const F fr = (int_to_f(s.h(0xBA)) * kTenth) * kG;
        const F last = p.f(kE + 0x08 + 4 * u32(s.b(0xBF)));
        p.set_f(kE + 0xD0, program.b(Program::kStatic + 0x13C) ? fr * F::raw(0x3F666666) : fr / last);
    }
    turbo(s, p);
    const s32 points = p.sh(kE);
    for (s32 i = 0; i < points; ++i) {
        const s32 rpm = s16(u16(u32(s.b(0xC0 + u32(i))) * 100));
        const F capped = boost_capped(p, rpm);
        const F raw = boost_raw(p, rpm);
        const u32 at = kE + 0x6C + 4 * u32(i);
        p.set_f(at, p.f(at) * (capped / raw));
    }
    p.set_b(kE + 0xDB, 0), p.set_w(kE + 0xDC, 0);
    if (power_rule > 0) {
        p.set_b(kE + 0xDB, 1);
        const F soft = int_to_f(program.b(Program::kStatic + 8)) * kCenti;
        F cap = int_to_f(power_rule) * kG;
        cap = cap * F::raw(0x44330CCC);
        cap = cap * kTwoPi;
        p.set_f(kE + 0xE0, soft);
        cap = cap / kSixty;
        p.set_f(kE + 0xDC, cap / (kOne - soft * kHalf));
    }
}

// ---- the driveline (0x34ACA8 with 0x34BC98, 0x34AAD8, 0x34AC48, 0x34BA30) ----

// 0x34BC98: the drive: the layout D +0x00, the driven axle D +0x03, the split D +0x04 and the center clutch's capacity
// D +0x08. Four wheel drive system 5 sets that system's fade (0x62030C / 0x620314).
void drive(const Spec& s, const Program& program, ParamBlock& p, ConvertGlobals* g) {
    p.set_w(kD + 4, 0), p.set_w(kD + 8, 0);
    auto layout = [&](u8 l, u8 axle) { p.set_b(kD, l), p.set_b(kD + 3, axle); };
    const u8 d = s.b(0x128);
    if (d >= 5) return;
    if (d == 1) return layout(1, 0);
    if (d != 2) return layout(0, 1);
    p.set_f(kD + 4, int_to_f(s.b(0x12A)) * kCenti);
    p.set_b(kD + 3, 1);
    auto capacity = [&](u8 fallback) {
        const u8 c = s.b(0x12B) ? s.b(0x12B) : fallback;
        p.set_f(kD + 8, int_to_f(s32(c) * 10) * kG);
    };
    switch (s.b(0x129)) {
    case 0: return layout(2, 0);
    case 2:
        p.set_b(kD, 4);
        if (!(s.b(0x12A) < 50)) p.set_f(kD + 4, kOne);
        return capacity(8);
    case 3: {
        p.set_b(kD, 5);
        F k = p.f(kD + 4) * F::raw(0x3EB851ED);
        p.set_f(kD + 4, k);
        const F cap = int_to_f(s32(program.b(kAwdBytes + 1)) * 10) * kG;
        k = k * (int_to_f(program.b(kAwdBytes)) / kHundred);
        p.set_f(kD + 4, k);
        const F start = int_to_f(program.b(kAwdBytes + 8)) / kHundred, floor = int_to_f(program.b(kAwdBytes + 0x10)) / kHundred;
        if (g) g->awd_fade_set = true, g->awd_fade_start = start, g->awd_fade_floor = floor;
        p.set_f(kD + 8, cap);
        return;
    }
    case 4: {
        p.set_b(kD, 6);
        const F k = p.f(kD + 4);
        if (!(k == kZero)) p.set_f(kD + 4, kOne / ((k * kHundred) * kDegree));
        return capacity(15);
    }
    case 5: p.set_b(kD, 7); return; // (0x355978: the motors' constants into the program's globals, not kept here)
    case 6: return layout(9, 0);
    case 7: p.set_b(kD, 8); return;
    case 8: p.set_b(kD, 10); return;
    default: p.set_b(kD, 3); return;
    }
}

// 0x34AAD8: a differential's type letter as its index in the program's letters (0x620300, 8 of them); a letter not
// among them stays as it is.
u8 diff_index(const Program& program, u8 letter) {
    const s8 l = s8(letter);
    if (s8(program.b(kDiffLetters)) == l) return 0;
    for (u32 i = 1; i < 8; ++i)
        if (s8(program.b(kDiffLetters + i)) == l) return u8(i);
    return letter;
}

// 0x34BA30: an axle's tire: radius D +0x90, width D +0xA0, sidewall D +0xA8.
void tire(const Spec& s, const Program& program, ParamBlock& p, u32 a) {
    const u32 radius = kD + 0x90 + 4 * a, width = kD + 0xA0 + 4 * a, side = kD + 0xA8 + 4 * a;
    const u8 wb = s.b(0x1C8 + a), rim = s.b(0x1CA + a), aspect = s.b(0x1CC + a);
    p.set_f(width, int_to_f(wb ? s32(wb) * 5 : 10) * kMilli);
    F half;
    if (rim < 61) half = (int_to_f(rim) * kInch) * kHalf;
    else if (rim % 10 == 0) half = (int_to_f(rim / 10) * kInch) * kHalf;
    else half = (int_to_f(rim) * kInch) * F::raw(0x3D4CCCCC);
    const F wall = (p.f(width) * int_to_f(aspect)) * F::raw(0x3D4CCCCC);
    p.set_f(side, wall), p.set_f(radius, half + wall);
    if (const s8 bigger = s8(s.b(0x1E2 + a)); bigger != 0) {
        F more = (int_to_f(bigger) * kInch) * kHalf;
        if (half + more < kInch) more = kInch - half;
        const F w = p.f(side) - more;
        p.set_f(side, w);
        if (w < F::raw(0x3C5013A9)) p.set_f(side, F::raw(0x3C5013A9));
    }
    if (const u8 pc = s.b(0x1E4 + a); pc != 0 && pc != 100) p.set_f(width, p.f(width) * (int_to_f(pc) / kHundred));
    // A special car's tires: the program's row (0x359F20, by the spec's byte +0x23): front width +0x04, radius +0x00; rear
    // +0x0C, +0x08; the sidewall half the width.
    if (special_car(s)) {
        const u32 row = special_row(s.b(0x23));
        p.set_w(width, program.w(row + (a ? 0x0C : 0x04)));
        p.set_w(radius, program.w(row + (a ? 0x08 : 0x00)));
        p.set_f(side, p.f(width) * kHalf);
    }
}

void driveline(Spec& s, const Program& program, ParamBlock& p, ConvertGlobals* g) {
    drive(s, program, p, g);
    for (u32 a = 0; a < 2; ++a) p.set_b(kD + 0x10 + a, diff_index(program, s.b(0x168 + a)));
    // The driven axles by the layout (0x34AC48): rear for 0 and 7, front for 1, both for the others up to 10.
    {
        const u8 l = p.b(kD);
        bool driven[2] = {false, false};
        if (l < 11) {
            if (l == 0 || l == 7) driven[1] = true;
            else if (l == 1) driven[0] = true;
            else driven[0] = driven[1] = true;
        }
        for (u32 a = 0; a < 2; ++a) {
            const u8 t = p.b(kD + 0x10 + a);
            if (driven[a]) {
                if (t == 0) p.set_b(kD + 0x10 + a, 2);
            } else if (t != 0) p.set_b(kD + 0x10 + a, 0);
        }
    }
    // A group of one wheel has the type 1 when its axle's differential has one.
    {
        const u32 row = rules::kWheelLayouts + rules::kWheelLayoutSize * s.b(0x02);
        const u8 groups = program.b(row + 2);
        for (u32 k = 0; k < groups; ++k) {
            if (program.b(row + 8 + k) != 1) continue;
            const u8 axle = program.b(row + 0x10 + k);
            if (p.b(kD + 0x10 + axle) != 0) p.set_b(kD + 0x10 + axle, 1);
        }
    }
    // Each axle's differential's settings by its type: 0 .. 2 none; 3, 4 and 6 the initial torque (x 9.8 N m), the
    // acceleration and deceleration shares (percent); 5 the initial torque squared and the acceleration's x 0.1 x 9.8;
    // 7 all three in percent.
    for (u32 a = 0; a < 2; ++a) {
        const u8 t = p.b(kD + 0x10 + a);
        if (t >= 8) continue;
        const u32 i = kD + 0x14 + 4 * a, ac = kD + 0x1C + 4 * a, de = kD + 0x24 + 4 * a;
        const F b0 = int_to_f(s.b(0x16A + a)), b1 = int_to_f(s.b(0x16C + a)), b2 = int_to_f(s.b(0x16E + a));
        switch (t) {
        case 3:
        case 4:
        case 6: p.set_f(i, b0 * kG), p.set_f(ac, b1 * kCenti), p.set_f(de, b2 * kCenti); break;
        case 5: p.set_f(i, b0 * b0), p.set_f(ac, (b1 * kTenth) * kG), p.set_w(de, 0); break;
        case 7: p.set_f(i, b0 * kCenti), p.set_f(ac, b1 * kCenti), p.set_f(de, b2 * kCenti); break;
        default: p.set_w(i, 0), p.set_w(ac, 0), p.set_w(de, 0); break;
        }
    }
    // (0x34ABC8: a gearbox whose ratios the converter makes, spec +0x156 and +0x157; the spec builder leaves +0x156 0.)
    // The gearbox: its gears and kind (5 is 2, 6 is 0; at most 9 gears; a one-speed box one gear; a stepless box of kind 1
    // with fewer than two gears is 0).
    p.set_b(kD + 2, s.b(0x155));
    p.set_b(kD + 0x12, s.b(0x158));
    if (s.b(0x158) == 5) p.set_b(kD + 0x12, 2);
    else if (s.b(0x158) == 6) p.set_b(kD + 0x12, 0);
    {
        const u8 gears = p.b(kD + 2);
        if (!(gears < 10)) p.set_b(kD + 2, 9);
        else if (p.b(kD + 0x12) == 3) {
            if (gears != 1) p.set_b(kD + 2, 1);
        } else if (s.b(0x158) == 1 && gears < 2) p.set_b(kD + 0x12, 0);
    }
    // The ratios: reverse and the forward gears' words x 0.001 (3.5 and 4.0 below 100; a later gear below 100 ends the box
    // a gear before it) x the final drive (x 0.001; 4.0 below 500), reverse negative.
    const s16 fw = s.sh(0x150);
    const F fin = int_to_f(fw < 500 ? 4000 : fw) * kMilli;
    if (!(s8(p.b(kD + 2)) < 0)) {
        for (u32 gi = 0;;) {
            s32 w = s.sh(0x138 + 2 * gi);
            if (w < 100) {
                if (gi >= 2) {
                    p.set_b(kD + 2, gi - 1);
                    break;
                }
                w = gi == 1 ? 4000 : 3500;
            }
            p.set_f(kD + 0x40 + 4 * gi, (int_to_f(w) * kMilli) * fin);
            ++gi;
            if (s32(p.b(kD + 2)) < s32(gi)) break;
        }
    }
    p.set_f(kD + 0x40, -p.f(kD + 0x40));
    // The second reduction by its kind (+0x154): 1 every gear times it, 2 reverse and the gears from the fifth on times it
    // over the final drive, 3 reverse and the third and fourth.
    {
        const u8 rk = s.b(0x154);
        const s16 rw = s.sh(0x152);
        if (rk != 0 && rk != 4 && !(rw < 500)) {
            F f = int_to_f(rw) * kMilli;
            const u8 gears = p.b(kD + 2);
            if (rk == 1) {
                for (u32 gi = 0; gi <= gears; ++gi) p.set_f(kD + 0x40 + 4 * gi, p.f(kD + 0x40 + 4 * gi) * f);
            } else if (rk == 2) {
                f = f / fin;
                p.set_f(kD + 0x40, p.f(kD + 0x40) * f);
                for (u32 gi = 5; gi <= gears; ++gi) p.set_f(kD + 0x40 + 4 * gi, p.f(kD + 0x40 + 4 * gi) * f);
            } else if (rk == 3) {
                f = f / fin;
                p.set_f(kD + 0x40, p.f(kD + 0x40) * f);
                p.set_f(kD + 0x4C, p.f(kD + 0x4C) * f), p.set_f(kD + 0x50, p.f(kD + 0x50) * f);
            }
        }
    }
    // The tires and their inverse radii; the axles' tires told apart (layout 5 with unequal radii, a second reduction of
    // kind 4).
    for (u32 a = 0; a < 2; ++a) {
        tire(s, program, p, a);
        if (!(p.f(kD + 0x90 + 4 * a) == kZero)) p.set_f(kD + 0x98 + 4 * a, kOne / p.f(kD + 0x90 + 4 * a));
    }
    p.set_b(kD + 1, 0);
    if (p.b(kD) == 5 && !(p.f(kD + 0x90) == p.f(kD + 0x94))) p.set_b(kD + 1, 1);
    if (s.b(0x154) == 4) p.set_b(kD + 1, 1);
    // The wheels' inertias (x 0.01): each axle's wheel gain (radius over it) and a free wheel's extra mass (it over the
    // radius squared).
    F wheel_inertia[2];
    for (u32 a = 0; a < 2; ++a) wheel_inertia[a] = int_to_f(s.sh(0x174 + 2 * a)) * kCenti;
    for (u32 a = 0; a < 2; ++a) {
        const F i = wheel_inertia[a];
        if (i == kZero) continue;
        p.set_f(kD + 0xB4 + 4 * a, p.f(kD + 0x90 + 4 * a) / i);
        const F inv = p.f(kD + 0x98 + 4 * a);
        p.set_f(kD + 0xCC + 4 * a, (i * inv) * inv);
    }
    // The engine's inertia and its inverse.
    p.set_f(kD + 0x0C, int_to_f(s.h(0xB8)) * kMilli);
    if (s.h(0xB8) != 0) p.set_f(kD + 0xC8, kOne / p.f(kD + 0x0C));
    // The axles' inertias: the shaft's (x 0.01; the other axle's tires' over this one's radius when they differ) x the
    // final drive, plus both wheels'.
    for (u32 a = 0; a < 2; ++a) {
        F i = int_to_f(s.sh(0x164 + 2 * a)) * kCenti;
        if (p.b(kD + 1) != 0 && a != p.b(kD + 3)) i = i * (p.f(kD + 0x90 + 4 * u32(p.b(kD + 3))) / p.f(kD + 0x90 + 4 * a));
        p.set_f(kD + 0xBC + 4 * a, (i * fin) + (wheel_inertia[a] + wheel_inertia[a]));
    }
    p.set_f(kD + 0xC4, kOne / (p.f(kD + 0xBC) + p.f(kD + 0xC0)));
    p.set_h(kD + 0x2C, u32(s.b(0x160)) * 10);
    p.set_h(kD + 0x2E, u32(s.b(0xBE)) * 100);
    p.set_f(kD + 0xB0, int_to_f(s.b(0x162)) * kG);
    {
        const u8 gears = p.b(kD + 2);
        const F r = p.f(kD + 0x90 + 4 * u32(p.b(kD + 3)));
        for (u32 gi = 0; gi <= gears; ++gi) p.set_f(kD + 0x68 + 4 * gi, p.f(kD + 0x40 + 4 * gi) / r);
    }
    // The launch speed: a stepless box's idle; otherwise at least idle + 100.
    const bool stepless = u8(p.b(kD + 0x12) - 1) < 2;
    const u16 idle = p.h(kE + 0xCE);
    if (stepless) p.set_h(kD + 0x2C, idle);
    else if (s32(p.h(kD + 0x2C)) - 100 < s32(idle)) p.set_h(kD + 0x2C, u32(idle) + 100);
    // The down-shift speeds (gear g's into D +0x30 + 2 (g - 1)): the up-shift speed carried into the next gear (the first
    // averaged with the launch speed when the static byte +0x112 is set), less 10 km/h's rpm there, at least the launch
    // speed + 500.
    for (u32 gi = 1; gi < p.b(kD + 2); ++gi) {
        const F turn = int_to_f(p.h(kD + 0x2E)) * kRpm;
        F v = (turn * p.f(kD + 0x44 + 4 * gi)) / p.f(kD + 0x40 + 4 * gi);
        v = (v * kSixty) / kTwoPi;
        s32 rpm = to_int(v);
        const s32 launch = p.h(kD + 0x2C);
        if (program.b(Program::kStatic + 0x112) != 0 && gi == 1) rpm = (launch + rpm) / 2;
        const F ten = ((p.f(kD + 0x6C + 4 * gi) * F::raw(0x4031C71B)) * kSixty) / kTwoPi;
        rpm -= to_int(ten);
        const s32 least = launch + 500;
        p.set_h(kD + 0x30 + 2 * (gi - 1), u32(rpm < least ? least : rpm));
    }
    if (stepless && !(p.b(kD + 2) < 2))
        for (u32 gi = 2; gi <= p.b(kD + 2); ++gi) p.set_f(kD + 0x40 + 4 * gi, p.f(kD + 0x40 + 4 * gi) / p.f(kD + 0x44));
    // 0x367D90: the up-shift speed's class.
    const u16 up = p.h(kD + 0x2E);
    p.set_b(kD + 0x13, up < 4500 ? 1 : up < 5000 ? 2 : 3);
    if (s.b(0x01) == 5) p.set_b(kD + 0x13, 4);
}

// ---- the suspension (0x360B60 with 0x3608E8 / 0x360980; 0x360DC8) ----

// 0x360980: a damper's square-law fit through two points (speed, force): its three terms, or zeros.
void damper_fit(F v1, F f1, F v2, F f2, F& t, F& o, F& g) {
    const F zero = kZero;
    g = zero, o = zero, t = zero;
    if (f1 == zero || f2 == zero || v1 == zero || v2 == zero) return;
    if (v2 <= v1 || f2 <= f1) return;
    const F d = f1 * v2 - f2 * v1;
    if (d <= zero) return;
    const F e = ((f1 * f1) * v2) - ((f2 * f2) * v1);
    if (zero < e) return;
    F q = e / (d + d);
    F r = (f1 * f2) * (f2 - f1);
    r = r / d;
    g = gt3r::ee::sqrt(r);
    q = -(q * q);
    q = q / r;
    t = q;
    q = gt3r::ee::sqrt(-q);
    o = -g * q;
}

void suspension(const Spec& s, const Program& program, ParamBlock& p, u32 at, u32 blk, u8 ratio) {
    const F mr = int_to_f(ratio) * kCenti;
    p.set_f(at + 0x14, (int_to_f(s.b(blk)) * F::raw(0x4474FFFE)) * mr);
    const F scale = kHundred / int_to_f(ratio);
    const bool linear = program.b(Program::kStatic + 0x101) != 0;
    for (u32 k = 0; k < 2; ++k) {
        const u32 b = blk + 4 + 4 * k;
        const F f1 = (int_to_f(s.b(b + 1)) * F::raw(0x40A00000)) * kG, f2 = (int_to_f(s.b(b + 3)) * F::raw(0x40A00000)) * kG;
        F v1 = int_to_f(s.b(b)) * kCenti, v2 = int_to_f(s.b(b + 2)) * kCenti;
        v1 = v1 * scale, v2 = v2 * scale;
        const u32 o = at + 0x18 + 0x0C * k;
        if (linear) {
            p.set_f(o, v1), p.set_f(o + 4, f1), p.set_f(o + 8, v2), p.set_f(at + 0x48 + 4 * k, f2);
        } else {
            F t, oo, g;
            damper_fit(v1, f1, v2, f2, t, oo, g);
            p.set_f(o, t), p.set_f(o + 4, oo), p.set_f(o + 8, g);
        }
    }
    p.set_f(at + 0x30, (int_to_f(s.b(blk + 1)) * F::raw(0x41200000)) * kG);
    p.set_f(at + 0x34, (int_to_f(s.b(blk + 2)) * F::raw(0x461C4000)) * kG);
    p.set_f(at + 0x38, int_to_f(s.b(blk + 3)) * kQuarter);
    p.set_f(at + 0x3C, (int_to_f(s.b(blk + 3)) * kQuarter) * kG);
    p.set_f(at + 0x40, (int_to_f(s.b(blk + 0x0D)) * F::raw(0x41200000)) * kG);
}

void travel(const Spec& s, const Program& program, ParamBlock& p, u32 a) {
    const u32 S = 0x308 + 0x58 * a;
    const F k = p.f(S + 0x14);
    if (!(k == kZero)) {
        const u8 wheels = program.b(layout_row(p) + 4 + a);
        F x = -p.f(0x1040 + 4 * a);
        x = x * (kOne / int_to_f(wheels));
        p.set_f(S + 0x10, x / k);
    }
    const F ride = p.f(0x40 + 4 * a), xs = p.f(S + 0x10);
    p.set_f(S + 0x0C, (p.f(0x2C4 + 4 * a) - ride) + xs);
    F t = int_to_f(s.b(0x92 + a)) * kMilli;
    const F base = xs - ride;
    if (ride < t) t = ride;
    p.set_f(S + 0x04, base + t);
    const s16 gap_mm = s.sh(0x66 + 0x18 * a);
    F gap = int_to_f(gap_mm) * kMilli;
    if (ride < gap) gap = ride;
    F knee;
    if (gap_mm == 0 || gap <= t) knee = p.f(S + 0x04);
    else knee = base + gap;
    p.set_f(S + 0x00, knee);
    const F hang = p.f(S + 0x3C) / k;
    const F droop = xs + int_to_f(s.b(0x94 + a)) * kMilli;
    p.set_f(S + 0x08, droop);
    if (hang < droop) p.set_f(S + 0x08, hang);
}

// ---- the tire records (0x361570 with 0x361008, 0x361220, 0x353800, 0x3613D8; 0x361798) ----

// 0x361008: curves C (the longitudinal force over the slip ratio) and D (the friction circle) from the block's negative
// and positive sides, sharing their zero point; the optimum slips and the braking peak.
void curves_cd(Spec& s, ParamBlock& p, u32 R, u32 blk) {
    s.set_b(blk + 0x20, 0), s.set_b(blk + 0x40, s.b(blk + 0x48)), s.set_b(blk + 0x30, 0), s.set_b(blk + 0x28, 0), s.set_b(blk + 0x38, 0);
    const u32 neg = s.b(blk + 9), pos = s.b(blk + 0x0A);
    F best = kZero;
    p.set_f(R + 0x164, kZero);
    for (u32 i = 0; i < neg; ++i) {
        const u32 k = neg - 1 - i;
        const F x = (-int_to_f(s.b(blk + 0x20 + i))) / k255;
        p.set_f(R + 0x104 + 4 * k, x);
        const F y = int_to_f(s.b(blk + 0x30 + i)) / k200;
        p.set_f(R + 0x134 + 4 * k, y);
        if (best < y) p.set_f(R + 0x164, x), best = y;
        p.set_f(R + 0x17C + 4 * k, int_to_f(s.b(blk + 0x40 + i)) / k200);
    }
    best = kZero;
    p.set_f(R + 0x168, best), p.set_f(R + 0x16C, kOne);
    for (u32 i = 0; i < pos; ++i) {
        const u32 k = neg - 1 + i;
        const F x = int_to_f(s.b(blk + 0x28 + i)) / k255;
        p.set_f(R + 0x104 + 4 * k, x);
        const F y = int_to_f(s.b(blk + 0x38 + i)) / k200;
        p.set_f(R + 0x134 + 4 * k, y);
        if (best < y) best = y, p.set_f(R + 0x168, x), p.set_f(R + 0x16C, best);
        p.set_f(R + 0x17C + 4 * k, int_to_f(s.b(blk + 0x48 + i)) / k200);
    }
    const u32 n = neg + pos - 1;
    table(p, R + 0xF8, n & 0xFFFF, R + 0x104, R + 0x134);
    table(p, R + 0x170, n & 0xFFFF, R + 0x104, R + 0x17C);
}

// 0x361220: curves A (over the slip angle: the block's +0x02 degrees, 90 when 0, at most pi / 2) and B (over pi / 2);
// their first points' bytes read as 0.
void curves_ab(Spec& s, ParamBlock& p, u32 R, u32 blk) {
    const u8 deg = s.b(blk + 2) ? s.b(blk + 2) : 90;
    s.set_b(blk + 0x10, 0);
    F range = int_to_f(deg) * kDegree;
    if (F::raw(0x3FC90FDA) < range) range = F::raw(0x3FC90FDA);
    s.set_b(blk + 0x18, 0);
    const u32 na = s.b(blk + 8);
    for (u32 i = 0; i < na; ++i) {
        p.set_f(R + 0x78 + 4 * i, (int_to_f(s.b(blk + 0x10 + i)) * range) / k255);
        p.set_f(R + 0x98 + 4 * i, int_to_f(s.b(blk + 0x18 + i)) / k200);
    }
    table(p, R + 0x6C, na, R + 0x78, R + 0x98);
    s.set_b(blk + 0x50, 0);
    const u32 nb = s.b(blk + 0x0C);
    s.set_b(blk + 0x56, 0);
    for (u32 i = 0; i < nb; ++i) {
        p.set_f(R + 0xC8 + 4 * i, (int_to_f(s.b(blk + 0x50 + i)) * F::raw(0x3FC90FD9)) / k255);
        p.set_f(R + 0xE0 + 4 * i, int_to_f(s.b(blk + 0x56 + i)) / k200);
    }
    table(p, R + 0xBC, nb, R + 0xC8, R + 0xE0);
}

// 0x352218: an angle folded into 0 .. pi / 2, and the sign the curve's value takes there.
F fold(F a, F& sign) {
    sign = kOne;
    if (a < F::raw(0xBFC90FDA)) return a + F::raw(0x40490FDA);
    if (a < kZero) return sign = F::raw(0xBF800000), -a;
    if (a < F::raw(0x3FC90FDA)) return a;
    sign = F::raw(0xBF800000);
    return F::raw(0x40490FDA) - a;
}

// 0x353800: the slip angle of curve A's peak: a tire read at its sideways speed, the axis point of the greatest value;
// otherwise the whole degree 0 .. 45 where curve A at the folded angle (x its sign) is greatest (90 when none is above 0).
F peak_slip(const ParamBlock& p, u32 R) {
    const u32 A = R + 0x6C;
    if (p.b(R + 0x1D9) != 0) {
        F best = kZero, at = kZero;
        const s32 n = p.sh(A);
        for (s32 k = 0; k < n; ++k) {
            const F y = p.f(ty(p, A) + 4 * u32(k));
            if (best < y) best = y, at = p.f(tx(p, A) + 4 * u32(k));
        }
        return at;
    }
    F best = kZero;
    s32 deg = 90;
    for (s32 d = 0; d < 0x2E; ++d) {
        F sign;
        const F v = fold(int_to_f(d) * kDegree - kZero, sign);
        const F y = lookup_scan(p, A, v) * sign;
        if (best < y) best = y, deg = d;
    }
    return int_to_f(deg) * kDegree;
}

// 0x3613D8: the grips (the block's +0x00 percent, 100 when 0) on the 24 surfaces (+0x64 + s percent of it, all of it when
// 0), the wear rate, the wear's floor (`floor` over the grip, at most 1), the load curve and the base grip (9.8 x curve D
// at no slip).
void grips(Spec& s, ParamBlock& p, u32 R, u32 blk, F floor) {
    const F g = int_to_f(s.b(blk) ? s.b(blk) : 100) * kCenti;
    for (u32 sf = 0; sf < 24; ++sf) {
        const u8 v = s.b(blk + 0x64 + sf);
        p.set_f(R + 0x08 + 4 * sf, v ? (int_to_f(v) * kCenti) * g : g);
    }
    p.set_f(R + 0x68, floor / g);
    p.set_f(R + 0x04, int_to_f(s.b(blk + 1)) * kCenti);
    if (kOne < p.f(R + 0x68)) p.set_f(R + 0x68, kOne);
    const u32 n = s.b(blk + 0x0B);
    s.set_b(blk + 0x5C, 0);
    for (u32 i = 0; i < n; ++i) {
        p.set_f(R + 0x1B8 + 4 * i, (int_to_f(s.b(blk + 0x5C + i)) * F::raw(0x41A00000)) * kG);
        p.set_f(R + 0x1C8 + 4 * i, int_to_f(s.b(blk + 0x60 + i)) / k200);
    }
    table(p, R + 0x1AC, n, R + 0x1B8, R + 0x1C8);
    p.set_f(R + 0x00, lookup_bisect(p, R + 0x170, kZero) * kG);
}

void tire_records(Spec& s, const Program& program, ParamBlock& p) {
    // The tire index per surface (P +0xF98 + s front, +0xFB0 + s rear): the surface's block class (spec +0x1F0 + s; 0 past
    // 2), the rear's three on; the records any surface uses are built.
    u8 used[6] = {};
    for (u32 sf = 0; sf < 24; ++sf) {
        u8 k = s.b(0x1F0 + sf);
        if (!(k < 3)) k = 0;
        p.set_b(0xF98 + sf, k), p.set_b(0xFB0 + sf, k + 3);
        ++used[k], ++used[k + 3];
    }
    for (u32 r = 0; r < 6; ++r) {
        if (s8(used[r]) == 0) continue;
        const u32 blk_index = s.b(0x1E0) ? r : r % 3;
        const u32 R = 0x410 + 0x1EC * r;
        const u32 blk = 0x210 + 0xA8 * blk_index;
        p.set_b(R + 0x1D8, s.b(blk + 3)), p.set_b(R + 0x1D9, s.b(blk + 4)), p.set_b(R + 0x1DA, s.b(blk + 5));
        curves_cd(s, p, R, blk);
        curves_ab(s, p, R, blk);
        F pk = peak_slip(p, R);
        p.set_f(R + 0xB8, pk);
        if (F::raw(0x3F490FD9) < pk) p.set_f(R + 0xB8, F::raw(0x3F490FD9));
        const F floor = int_to_f(program.b(Program::kStatic + 0xBF + blk_index % 3)) * kCenti;
        grips(s, p, R, blk, floor);
    }
}

// 0x361798: each record's base grip by its load curve at its axle's static load per wheel (half the axle's, plus the
// unsprung weight) over the tire's width; P +0x104A when the front's and the rear's paved grips differ.
void base_grips(const Program& program, ParamBlock& p) {
    const u32 row = layout_row(p);
    for (u32 r = 0; r < 6; ++r) {
        const u32 R = 0x410 + 0x1EC * r;
        if (p.f(R) == kZero) continue;
        const u32 axle = r / 3;
        const F load = p.f(0x1040 + 4 * axle) * kHalf + p.f(0x344 + 0x58 * axle);
        const u8 wheel = program.b(row + 0x0C + axle);
        const u8 wa = wheel < program.b(row + 1) ? program.b(row + 0x14 + wheel) : 0;
        const F per = load / p.f(0x2D4 + 4 * u32(wa));
        p.set_f(R, p.f(R) * lookup_scan(p, R + 0x1AC, per));
    }
    p.set_b(0x104A, p.f(0x418) == p.f(0x9DC) ? 0 : 1);
}

// ---- the top speed (0x360780 with 0x34F3F8, 0x360520, 0x3F3278, 0x3F86E8) ----

// 0x350868: the engine's torque factor of its condition (1 for a fresh engine).
F condition_factor(const ConditionLimits& L, const u8* cond) {
    if (cond == nullptr || cond[0x14] != 0) return kOne;
    F w4, w8;
    std::memcpy(&w4.bits, cond + 4, 4), std::memcpy(&w8.bits, cond + 8, 4);
    F a = kOne;
    F x = w4;
    if (x < L.l[0]) a = a - L.l[1];
    else if (x < kZero) a = a - ((x * L.l[1]) / L.l[0]);
    else if (!(x <= L.l[2])) {
        if (!(x < L.l[3])) a = a - L.l[4];
        else a = a - (((x - L.l[2]) * L.l[4]) / (L.l[3] - L.l[2]));
    }
    x = w8;
    F b = kOne;
    if (x < L.l[5]) b = L.l[6] + b;
    else if (x < kZero) b = ((x * L.l[6]) / L.l[5]) + b;
    else if (L.l[8] < x) {
        F t = (x - L.l[8]) / (L.l[7] - L.l[8]);
        if (b < t) t = b;
        b = b - L.l[6] * t;
    }
    return a * b;
}

// 0x34E9A0: the engine's full-throttle torque at omega (its curve, 0x359568) x its condition's factor.
F full_torque(const ParamBlock& p, F omega, F factor) { return lookup_bisect(p, kE, omega) * factor; }

// 0x34EA68: a torque under the power cap at omega.
F power_capped(const ParamBlock& p, F torque, F omega) {
    if (p.b(kE + 0xDB) == 0) return torque;
    const F soft = p.f(kE + 0xE0), cap = p.f(kE + 0xDC);
    const F power = torque * omega;
    if (power <= cap * (kOne - soft)) return torque;
    F limit = cap * (kOne - soft * kHalf);
    if (!(cap <= power)) {
        const F d = cap - power;
        limit = limit - (d * d) / ((soft + soft) * cap);
    }
    const F t = limit / omega;
    return t < torque ? t : torque;
}

struct EnginePeak {
    F ps, kgfm;
    s32 power_rpm = 0, torque_rpm = 0;
};
// 0x34F3F8: the engine's peak power (PS) and torque (kgf m) every 100 rpm from the curve's first point (rounded) to the
// higher of the rev limit and its last point, counting the speeds up to the rev limit.
EnginePeak engine_peak(const ParamBlock& p, F factor) {
    EnginePeak e;
    std::vector<s32> rpms;
    const s32 n = p.sh(kE);
    for (s32 i = 0; i < n; ++i) {
        const F w = (p.f(kE + 0x0C + 4 * u32(i)) * kSixty) / kTwoPi;
        const s32 r = ((to_int(w) + 50) / 100) * 100;
        if (rpms.empty() || rpms.back() < r) rpms.push_back(r);
    }
    const s32 limit = p.h(kE + 0xCC);
    s32 top = limit;
    const s32 last = rpms.empty() ? 0 : rpms.back();
    if (top < last) top = last;
    s32 rpm = rpms.empty() ? 0 : rpms.front();
    if (rpms.empty()) rpm = 0; // (the program reads a half word it did not write: 0 in a fresh frame)
    F best_t = kZero, best_p = kZero;
    for (; !(top < rpm); rpm += 100) {
        const F r = int_to_f(rpm);
        const F omega = r * kRpm;
        const F t = power_capped(p, full_torque(p, omega, factor), omega) / kG;
        const F ps = (t * r) / F::raw(0x44330CCC);
        if (!(limit < rpm)) {
            if (best_t < t) best_t = t, e.torque_rpm = rpm;
            if (best_p < ps) best_p = ps, e.power_rpm = rpm;
        }
    }
    e.ps = best_p, e.kgfm = best_t;
    return e;
}

// 0x3F1FB0: a stepless box of kind 1 at the engine speed `omega` (from its speed in the first ratio): held at its hold
// speed with the ratio its share, at least the last gear's.
bool stepless_hold(const ParamBlock& p, F& omega, F& share) {
    if (p.b(kD + 0x12) != 1) return false;
    const F hold = int_to_f(p.h(kD + 0x30)) * kRpm;
    if (omega < hold) return share = kOne, true;
    F r = hold / omega;
    share = r;
    const F least = p.f(kD + 0x40 + 4 * u32(p.b(kD + 2)));
    if (r < least) share = least;
    omega = share * omega;
    return true;
}

// 0x360520: the top speed: from rest in steps of the first estimate / 500 km/h, the highest speed where the engine's
// torque in the gear it would be in beats the rolling resistance, the downforces' share of it and the drag.
void top_speed(const ParamBlock& pc, ParamBlock& p, const rules::Tables& race, F factor) {
    (void)pc;
    if (p.b(kD + 0x12) == 3) return p.set_f(0xFD8, F::raw(0x4247FFFF));
    const s32 top = to_int(p.f(0xFD8) * F::raw(0x40666666));
    const s32 step = top / 500 + 1;
    s32 gear = 1;
    for (s32 kmh = 0; 0 < top && kmh < top;) {
        const F v = int_to_f(kmh) * kKmh;
        const F gs = p.f(0x29C + 4 * u32(gear));
        kmh += step;
        F share = kOne;
        F omega = v * gs;
        const F v2 = v * v;
        stepless_hold(p, omega, share);
        const s32 rpm = to_int((omega * kSixty) / kTwoPi);
        const F t = power_capped(p, full_torque(p, omega, factor), omega);
        F drive_force = t * gs;
        if (p.b(kD + 0x12) == 1) drive_force = drive_force * share;
        const F roll = rules::table(race.road.rolling_curve, v);
        const F front = p.f(0x106C) * v2, rear = p.f(0x1070) * v2;
        F resist = roll * race.road.rolling_mu[0];
        resist = resist * ((p.f(0x103C) + front) + rear);
        resist = resist + p.f(0x1068) * v2;
        if (resist < drive_force) p.set_f(0xFD8, v);
        if (p.b(kD + 0x12) != 1 && gear < s32(p.b(kD + 2))) gear += s32(p.h(kD + 0x2E)) < rpm ? 1 : 0;
    }
}

// 0x3F3278: P +0x10DC from the program's table 0x622320 by the spec's kind +0x120 (0 .. 3) and whether the engine has a
// turbo: 0 below the row's first power (x 10 PS), 0.75 from its second, between them 0.25 .. 0.75.
void kind_factor(const Program& program, ParamBlock& p, s32 kind, F ps) {
    if (kind < 0) kind = 0;
    else if (!(kind < 4)) kind = 3;
    const u32 turbo = kZero < p.f(0x230) ? 1 : 0;
    const u32 at = kKindTable + 2 * (2 * u32(kind) + turbo);
    const F lo = int_to_f(s32(program.b(at)) * 10), hi = int_to_f(s32(program.b(at + 1)) * 10);
    if (ps < lo) return p.set_f(0x10DC, kZero);
    if (hi <= ps) return p.set_f(0x10DC, F::raw(0x3F400000));
    p.set_f(0x10DC, (((ps - lo) * kHalf) / (hi - lo)) + kQuarter);
}

// 0x356EA8: the motors' extra power for the factor below (layout 7's, 0x356E08; 13.6 for an up-shift class of 4).
F motor_power(const Program& program, const ParamBlock& p) {
    if (p.b(kD) == 7) {
        const F a = int_to_f(program.b(0x62045A)), b = int_to_f(program.b(0x62045B));
        return ((a + a) + b) / F::raw(0x3F3C49BA);
    }
    return p.b(kD + 0x13) == 4 ? F::raw(0x41598A02) : kZero;
}

// 0x3F86E8: P +0xFE0, a figure of the car's power, grip and aerodynamics (0.01 .. 1).
F pace_figure(const Program& program, const ParamBlock& p, const EnginePeak& e) {
    const F power = e.ps + motor_power(program, p);
    F down = p.f(0x106C) + p.f(0x1070);
    down = down * kHalf;
    F lift = down - p.f(0x1068);
    lift = lift + kOne;
    F grip = (p.f(0xFD0) + p.f(0xFD4)) * kHalf;
    if (p.b(0x1077) == 7) grip = grip * ((p.f(0x1084) / p.f(0x103C)) + kOne);
    F g0 = p.f(0x418);
    F m = p.f(0x48);
    const F root_m = gt3r::ee::sqrt(m);
    g0 = g0 * g0;
    const F root_t = gt3r::ee::sqrt(e.kgfm);
    m = m * root_m;
    g0 = g0 * power;
    g0 = g0 * power;
    F x = g0 * root_t;
    x = x / m;
    x = x * lift;
    x = x * grip;
    x = gt3r::ee::sqrt(x);
    x = gt3r::ee::sqrt(x);
    x = gt3r::ee::sqrt(x);
    F r = x / F::raw(0x40200000);
    if (kOne < r) r = kOne;
    if (r < kZero) r = kCenti;
    return r;
}

void top_and_hold(const Spec& s, const Program& program, ParamBlock& p, const rules::Tables& race, const ConvertIn& in) {
    {
        const F v = (int_to_f(p.h(0x200)) * kRpm) / p.f(0x29C + 4 * u32(p.b(0x236)));
        p.set_f(0xFD8, v);
        if (F::raw(0x43A6AAAA) < v) p.set_f(0xFD8, F::raw(0x43A6AAAA));
    }
    {
        const F mass = p.f(0x48);
        const u8 wheels = program.b(layout_row(p) + 4);
        F t = p.f(0x1010) * p.f(0x2CC);
        t = t * int_to_f(wheels);
        p.set_f(0x1020, t);
        p.set_f(0x101C, mass / (t + t));
    }
    const F factor = condition_factor(condition_limits(program), in.condition);
    const EnginePeak e = engine_peak(p, factor);
    if (p.b(kD + 0x12) == 1) {
        s32 hold = s32(p.h(kD + 0x2E)) - 500;
        const s32 at_power = u16(e.power_rpm);
        p.set_h(kD + 0x30, u32(hold < at_power ? hold : at_power));
    }
    top_speed(p, p, race, factor);
    kind_factor(program, p, s.b(0x120), e.ps);
    p.set_f(0xFE0, pace_figure(program, p, e));
}

} // namespace

bool special_car(const Spec& s) { return s.b(0x02) == 1 || s.b(0x01) == 6; }

// 0x350678: the engine condition's limits (the program's 0x620320 .. 0x620340), from the static block's bytes +0xD0 ..
// +0xD8.
ConditionLimits condition_limits(const Program& program) {
    const u32 S = Program::kStatic;
    ConditionLimits c;
    auto big = [&](u32 o) { return int_to_f(s32(program.b(S + o)) * 100000); };
    c.l[1] = int_to_f(program.b(S + 0xD1)) * kMilli;
    c.l[0] = -big(0xD0);
    c.l[2] = big(0xD2);
    c.l[3] = big(0xD3) + c.l[2];
    c.l[4] = int_to_f(program.b(S + 0xD4)) * kMilli;
    c.l[6] = int_to_f(program.b(S + 0xD6)) * kMilli;
    c.l[5] = (-big(0xD5)) * kHalf;
    c.l[7] = big(0xD7);
    c.l[8] = big(0xD8);
    return c;
}



void convert(Spec s, const Program& program, const rules::Tables& race, const ConvertIn& in, ParamBlock& p, ConvertGlobals* g) {
    auto sp = [&](u32 o) { return F::raw(program.w(special_row(p.b(0x33)) + o)); };
    p.set_b(0x10D3, 0);
    p.set_b(0x33, s.b(0x23));
    if (!(s.b(0x210) < 150)) p.set_b(0x10D3, 1);
    p.set_b(0x32, s.b(0x01));
    p.set_b(0x30, s.b(0x02));
    if (s.b(0x03) != 0) p.set_b(0x10D2, s.b(0x03));
    else p.set_b(0x10D2, s.b(0x128) == 3 ? 2 : s.b(0x128) == 4 ? 3 : 1);
    p.set_b(0x1076, s.b(0xA8));
    if (!(s.b(0x128) < 5)) s.set_b(0x128, 0);
    // The mass: the spec's weight (1200 when 0) times the weight percentage, the ballast, the static block's added masses.
    p.set_h(0x1048, s.h(0x20));
    p.set_f(0x48, s.sh(0x20) != 0 ? int_to_f(s.sh(0x20)) : F::raw(0x44960000));
    if (const u8 pc = s.b(0x08); pc != 0 && pc != 100) {
        const s32 w = s32(p.sh(0x1048)) * pc;
        p.set_f(0x48, p.f(0x48) * (int_to_f(pc) / kHundred));
        p.set_h(0x1048, u32(w / 100));
    }
    F front = int_to_f(s.b(0x22)) * kCenti;
    p.set_f(0x00, int_to_f(s.sh(0x12)) * kMilli);
    p.set_f(0x04, int_to_f(s.sh(0x14)) * kMilli);
    p.set_f(0x18, int_to_f(s.sh(0x1A)) * kMilli);
    if (special_car(s)) p.set_f(0x18, sp(0x74));
    F yaw_extra = kZero;
    // Ballast: the mass, and the front's share and the yaw inertia moved by its place.
    if (s.b(0x05) != 0) {
        const F share = front, at = int_to_f(50 - s32(s8(s.b(0x06)))) * kCenti, kg = int_to_f(s.b(0x05));
        const F mass = p.f(0x48), wb = p.f(0x18);
        const F total = mass + kg;
        p.set_f(0x48, total);
        p.set_h(0x1048, u32(p.h(0x1048)) + s.b(0x05));
        front = ((mass * share) + (kg * at)) / total;
        const F db = wb * (front - at), dc = wb * (share - front);
        yaw_extra = ((mass * dc) * dc) + ((kg * db) * db);
    }
    {
        const F wb = p.f(0x18);
        const F rear_d = wb * front, rear = kOne - front;
        p.set_f(0x24, front), p.set_f(0x14, rear_d), p.set_f(0x20, rear);
        const F front_d = wb - rear_d;
        p.set_f(0x10, front_d);
        p.set_f(0x50, (-(front_d - rear_d)) * kHalf);
        if (special_car(s)) p.set_f(0x50, -((sp(0x14) * rear) + (sp(0x70) * front)));
    }
    p.set_f(0x00, p.f(0x00) - p.f(0x50));
    p.set_f(0x04, p.f(0x04) + p.f(0x50));
    p.set_f(0x08, int_to_f(s.sh(0x16)) * kMilli);
    p.set_f(0x28, (int_to_f(s32(s.sh(0x1C)) - 2 * s32(s8(s.b(0x96)))) * kMilli) * kHalf);
    p.set_f(0x2C, (int_to_f(s32(s.sh(0x1E)) - 2 * s32(s8(s.b(0x97)))) * kMilli) * kHalf);
    p.set_f(0x0C, int_to_f(s.sh(0x18)) * kMilli);
    {
        const u32 S = Program::kStatic;
        const s32 add = p.b(kD) != 7 ? s32(program.b(S + 3)) + program.b(S + 4) : program.b(S + 3);
        p.set_f(0x48, p.f(0x48) + int_to_f(add));
    }
    engine(s, program, p, s32(in.rules[3]) * 10);
    driveline(s, program, p, g);
    p.set_f(0x104C, int_to_f(s.b(0x112)));
    p.set_b(0x104B, s.b(0x11C));
    // The half tracks narrowed where the tire stands out past the chassis' width across the tires.
    for (u32 a = 0; a < 2; ++a) {
        const u8 pc = s.b(0x1E4 + a);
        const s16 across = s.sh(0x30 + 2 * a);
        if (pc == 0 || pc == 100 || across == 0) continue;
        const F ht = p.f(0x28 + 4 * a);
        const F out = ht + p.f(0x2D4 + 4 * a) * kHalf;
        const F lim = (int_to_f(across) * kMilli) * kHalf;
        if (lim < out) p.set_f(0x28 + 4 * a, ht - (out - lim));
    }
    {
        const F fs = p.f(0x24);
        F t = p.f(0x28) * fs + p.f(0x2C) * (kOne - fs);
        p.set_f(0x1C, t + t);
    }
    p.set_f(0x10D4, int_to_f(s.b(0x11D)) * kCenti);
    p.set_f(0x10D8, int_to_f(s32(s.sh(0x11E)) * 100));
    // 0x36CD98: two bytes from the spec, or the up-shift speed / 100 and (the rev limit + 900) / 1000.
    p.set_b(0x1074, s.b(0x110) ? s.b(0x110) : p.h(0x262) / 100);
    p.set_b(0x1075, s.b(0x111) ? s.b(0x111) : (u32(p.h(0x200)) + 900) / 1000);
    // 0x466B28: the drive class.
    {
        const u8 d = s.b(0x128), sys = s.b(0x129);
        u8 cls = 0;
        if (d == 1) cls = 1;
        else if (d == 2) cls = sys == 3 ? 3 : sys < 4 ? (sys == 0 ? 1 : 2) : sys == 5 ? 0 : 2;
        p.set_b(0x31, cls);
    }
    {
        auto speed = [&](u32 o) {
            const u8 v = program.b(Program::kStatic + o);
            return int_to_f(v ? v : 50) * kKmh;
        };
        p.set_f(0xFCC, F::raw(0x3FC90FD9) / speed(0x118));
        p.set_f(0xFC8, kOne / speed(0x119));
    }
    tire_records(s, program, p);
    // The camber's grip curves: angles (0x80 - byte) x 4 degrees / 32, from the last point back; values / 200.
    for (u32 a = 0; a < 2; ++a) {
        const u32 n = s.b(0x1CE + a);
        for (u32 i = 0; i < n; ++i) {
            const u32 k = n - 1 - i;
            p.set_f(0x3D0 + 0x10 * a + 4 * k, (int_to_f(0x80 - s32(s.b(0x1D0 + 4 * a + i))) * F::raw(0x3D8EFA34)) * F::raw(0x3D000000));
            p.set_f(0x3F0 + 0x10 * a + 4 * k, int_to_f(s.b(0x1D8 + 4 * a + i)) / k200);
        }
        table(p, 0x3B8 + 0x0C * a, n, 0x3D0 + 0x10 * a, 0x3F0 + 0x10 * a);
    }
    // The wear's load terms (the static block's bytes +0xBC / +0xBD x 98 N; over the tire's width x 9,800).
    for (u32 a = 0; a < 2; ++a) {
        const F lo = int_to_f(program.b(Program::kStatic + 0xBC)) * F::raw(0x42C3FFFF);
        F hi = int_to_f(program.b(Program::kStatic + 0xBD)) * F::raw(0x42C3FFFF);
        hi = hi - lo;
        p.set_f(0x1088 + 4 * a, lo - hi);
        p.set_f(0x1090 + 4 * a, hi / (p.f(0x2D4 + 4 * a) * F::raw(0x46191FFF)));
    }
    // The axles' grips (+900 per mille); a computer's car's x the rule block's percent for its drive class.
    for (u32 a = 0; a < 2; ++a) p.set_f(0xFD0 + 4 * a, int_to_f(s32(s.b(0x2A + a)) + 900) * kMilli);
    if (in.control == 2) {
        const u8 cls = p.b(0x31);
        u32 pc[2] = {in.rules[4 + cls], in.rules[8 + cls]};
        for (u32 a = 0; a < 2; ++a) {
            if (pc[a] == 0) pc[a] = 100;
            p.set_f(0xFD0 + 4 * a, p.f(0xFD0 + 4 * a) * (int_to_f(s32(pc[a])) * kCenti));
        }
    }
    // The drag and downforce coefficients: the frontal area (spec s16 +0x40 / 10,000, or the body's width x height) x the
    // air's density x 0.5, times the bytes +0x42 / +0x43 / +0x44 x 0.01; the drag then with the downforces' share.
    F q;
    if (const s16 area = s.sh(0x40); area != 0) q = (int_to_f(area) / F::raw(0x461C4000)) * (race.aero.density * kHalf);
    else q = ((p.f(0x08) * p.f(0x0C)) * race.aero.density) * kHalf;
    {
        const F cd = int_to_f(s.b(0x42)) * kCenti;
        p.set_f(0x1064, cd), p.set_f(0x1068, q * cd);
        p.set_f(0x106C, q * (int_to_f(s.b(0x43)) * kCenti));
        p.set_f(0x1070, q * (int_to_f(s.b(0x44)) * kCenti));
    }
    // The part that changes with speed: its kind, its two speeds (km/h), its rate (10 / byte; 300 when 0), its lift.
    p.set_b(0x1077, s.b(0x4C));
    if (s.b(0x4C) != 0) {
        p.set_f(0x1078, int_to_f(s.b(0x4D)) * kKmh);
        p.set_f(0x107C, int_to_f(s.b(0x4E)) * kKmh);
        p.set_f(0x1080, s.b(0x4F) ? F::raw(0x41200000) / int_to_f(s.b(0x4F)) : F::raw(0x43960000));
        p.set_f(0x1084, q * (int_to_f(s.b(0x50)) * kCenti));
        if (p.b(0x1077) == 7) p.set_f(0x1084, int_to_f(s.b(0x50)) * F::raw(0x42C3FFFF));
    } else p.set_w(0x1078, 0), p.set_w(0x107C, 0), p.set_w(0x1080, 0), p.set_w(0x1084, 0);
    p.set_f(0x1068, p.f(0x1068) + (p.f(0x106C) + p.f(0x1070)) * (int_to_f(program.b(Program::kStatic + 0x0A)) * kMilli));
    // The brakes: each axle's torque (x 98 N m), the brake controller's two values, the ABS's terms.
    for (u32 a = 0; a < 2; ++a) {
        const u8 t = s.b(0x178 + a);
        p.set_f(0x1010 + 4 * a, int_to_f(s32(t) * 10) * kG);
        p.set_f(0x1024 + 4 * a, int_to_f(s.b(0x1A8 + a)) * kTenth);
        p.set_f(0x102C + 4 * a, int_to_f(s.b(0x1AA + a)) * kCenti);
        if (t != 0) p.set_f(0x1034 + 4 * a, p.f(0x2C4 + 4 * a) / p.f(0x1010 + 4 * a));
        else p.set_w(0x1034 + 4 * a, 0);
    }
    p.set_f(0x1018, int_to_f(s32(s.b(0x17A)) * 10) * kG);
    p.set_b(0x10D0, s.b(0x2C));
    p.set_f(0x10CC, int_to_f(s.b(0x2D)) * kCenti);
    // The aids' block (+0x1098).
    {
        const u32 A = 0x1098;
        const u8 m = s.b(0x1B8);
        p.set_f(A + 0x00, int_to_f(s.b(0x1BA)) * kCenti);
        p.set_f(A + 0x04, int_to_f(s.b(0x1BB)) * kCenti);
        p.set_f(A + 0x08, int_to_f(s.b(0x1BC)) * kCenti);
        for (u32 i = 0; i < 4; ++i) p.set_b(A + 0x0C + i, (m >> i) & 1);
        p.set_b(A + 0x30, (p.w(A + 0x0C) & 0xFFFF) != 0 && !(p.f(A) == kZero) ? 1 : 0);
        p.set_b(A + 0x31, (p.w(A + 0x0C) >> 16) != 0 && !(p.f(A + 8) == kZero) ? 1 : 0);
        p.set_f(A + 0x10, int_to_f(s.b(0x1BD)) * kCenti);
        p.set_f(A + 0x14, int_to_f(s.b(0x1BE)) * kCenti);
        p.set_f(A + 0x18, int_to_f(s.b(0x1BF)) * kCenti);
        for (u32 i = 0; i < 4; ++i) p.set_b(A + 0x1C + i, (m >> (4 + i)) & 1);
        p.set_b(A + 0x32, (p.w(A + 0x1C) & 0xFFFF) != 0 && !(p.f(A + 0x10) == kZero) ? 1 : 0);
        p.set_b(A + 0x33, (p.w(A + 0x1C) >> 16) != 0 && !(p.f(A + 0x18) == kZero) ? 1 : 0);
        p.set_f(A + 0x24, int_to_f(s.b(0x1C0)) * kTenth);
        p.set_f(A + 0x28, int_to_f(s.b(0x1C1)) * kCenti);
        p.set_f(A + 0x20, int_to_f(s.b(0x1C2)) * kTenth);
        const u8 level = s.b(0x1C3);
        p.set_f(A + 0x2C, level == 100 || level == 0 ? kOne : int_to_f(level) * kCenti);
    }
    // The steering: the lock, its three curves (km/h; 0.2 degrees), the steering ratios.
    p.set_f(0x54, int_to_f(s.b(0x28)) * kDegree);
    auto steer_curve = [&](u32 at, u32 x, u32 y, u8 n, u32 xs, u32 ys) {
        for (u32 i = 0; i < n; ++i) {
            p.set_f(x + 4 * i, int_to_f(s.b(xs + i)) * kKmh);
            p.set_f(y + 4 * i, (int_to_f(s.b(ys + i)) * F::raw(0x3E4CCCCC)) * kDegree);
        }
        table(p, at, n, x, y);
    };
    steer_curve(0x58, 0x64, 0x7C, s.b(0x180), 0x184, 0x18A);
    steer_curve(0x94, 0xA0, 0xB8, s.b(0x182), 0x19C, 0x1A2);
    steer_curve(0xD0, 0xDC, 0xF4, s.b(0x181), 0x190, 0x196);
    {
        const gt3r::sim::Behaviour b = rules::behaviour();
        const F wb = p.f(0x18), ht = p.f(0x28);
        p.set_f(0x10C, gt3r::sim::exact::atan2(b, wb, wb - ht) / F::raw(0x3F490FD9));
        p.set_f(0x110, gt3r::sim::exact::atan2(b, wb, wb + ht) / F::raw(0x3F490FD9));
        if (s.b(0x29) == 0) p.set_w(0x1004, 0);
        else {
            F ang = int_to_f(s.b(0x29)) * kDegree;
            if (special_car(s)) ang = sp(0x30);
            p.set_f(0x1004, gt3r::ee::sin(ang));
        }
        // Each axle's toe (atan2 of its offset over the tire's radius), camber, camber gain over travel, the camber's grip
        // at the camber, and the swing arm.
        for (u32 a = 0; a < 2; ++a) {
            const F toe = (int_to_f(s32(s.b(0x8E + a)) - 0x80) * kHalf) * kMilli;
            p.set_f(0x1008 + 4 * a, gt3r::sim::exact::atan2(b, toe, p.f(0x2C4 + 4 * a)));
            p.set_f(0xFE4 + 4 * a, -((int_to_f(s.b(0x88 + a)) * kTenth) * kDegree));
            p.set_f(0xFEC + 4 * a, int_to_f(s8(s.b(0x90 + a))) * kDegree);
            p.set_f(0xFF4 + 4 * a, lookup_bisect(p, 0x3B8 + 0x0C * a, p.f(0xFE4 + 4 * a)));
            p.set_f(0xFFC + 4 * a, int_to_f(s.b(0x98 + a)) * kCenti);
        }
    }
    // The inertias: yaw (the byte x 100, 6,000 when 0, with the ballast's), its inverse (of the yaw without the ballast),
    // pitch and roll (x 100; 4,000 and 2,000 when 0) and the levers.
    F yaw = s.b(0x24) ? int_to_f(s.b(0x24)) * kHundred : F::raw(0x45BB8000);
    p.set_f(0x114, yaw);
    if (s.b(0x05) != 0) p.set_f(0x114, yaw + yaw_extra);
    p.set_f(0x118, kOne / yaw);
    const F pitch = s.b(0x25) ? int_to_f(s.b(0x25)) * kHundred : F::raw(0x457A0000);
    p.set_f(0x11C, p.f(0x10) / pitch), p.set_f(0x120, p.f(0x14) / pitch);
    const F roll = s.b(0x26) ? int_to_f(s.b(0x26)) * kHundred : F::raw(0x44FA0000);
    p.set_f(0x124, p.f(0x28) / roll), p.set_f(0x128, p.f(0x2C) / roll);
    // 0x360A78: the dampers' square-law table.
    if (g) {
        const u32 S = Program::kStatic;
        g->damper_on = program.b(S + 0x102) != 0 ? 1 : 0;
        if (g->damper_on)
            for (u32 a = 0; a < 2; ++a) {
                g->damper_bump_knee[a] = int_to_f(program.b(S + 0x105 + a)) * kCenti;
                g->damper_bump_share[a] = int_to_f(program.b(S + 0x107 + a)) / kHundred;
                g->damper_rebound_knee[a] = int_to_f(program.b(S + 0x109 + a)) * kCenti;
                g->damper_rebound_share[a] = int_to_f(program.b(S + 0x10B + a)) / kHundred;
            }
    }
    suspension(s, program, p, 0x308, 0x58, s.b(0x8C));
    suspension(s, program, p, 0x360, 0x70, s.b(0x8D));
    // The ride heights, each axle's own (u16 mm, or the byte), the speed-switched part of kind 7 dropped when they differ by
    // more than 3 cm; the center of gravity's height, the weight, the roll center and the load transfers.
    for (u32 a = 0; a < 2; ++a) {
        const s16 mm = s.sh(0x9A + 2 * a);
        p.set_f(0x40 + 4 * a, mm != 0 ? int_to_f(mm) * kMilli : int_to_f(s.b(0x8A + a)) * kMilli);
    }
    if (p.b(0x1077) == 7) {
        F d = p.f(0x40) - p.f(0x44);
        if (d < kZero) d = -d;
        if (F::raw(0x3CF5C28F) < d) p.set_f(0x1084, kZero), p.set_b(0x1077, 0);
    }
    {
        const F cg = p.f(0x40) * p.f(0x24) + p.f(0x44) * p.f(0x20);
        p.set_f(0x3C, cg);
        p.set_f(0x103C, p.f(0x48) * kG);
        const F rc = int_to_f(s.b(0x27)) * kMilli;
        p.set_f(0x1060, rc);
        const F h = (rc + cg) * kHalf;
        p.set_f(0x38, h / p.f(0x18));
        p.set_f(0x34, h / p.f(0x1C));
    }
    // The sprung mass; its share of the load transfers; the static loads; the axles' load shares.
    {
        const u32 row = layout_row(p);
        const u8 nf = program.b(row + 4), nr = program.b(row + 5);
        const F mass = p.f(0x48);
        F sprung;
        if (nf == 2 && nr == 2) {
            const F sum = p.f(0x340) + p.f(0x398);
            sprung = mass - (sum + sum);
        } else sprung = mass - (p.f(0x340) * int_to_f(nf) + p.f(0x398) * int_to_f(nr));
        if (!(sprung <= kZero)) {
            const F share = sprung / mass, wt = sprung * kG;
            p.set_f(0x4C, sprung);
            p.set_f(0x1044, wt * p.f(0x20));
            p.set_f(0x38, share * p.f(0x38));
            p.set_f(0x34, sprung * p.f(0x34));
            p.set_f(0x1040, wt * p.f(0x24));
            p.set_f(0x1050, int_to_f(s.b(0x38))), p.set_f(0x1054, int_to_f(s.b(0x39)));
            F fl, rl;
            if (const s16 w = s.sh(0x3A); w != 0) {
                const F W = int_to_f(w) * kG;
                rl = W * p.f(0x20), fl = W * p.f(0x24);
            } else fl = p.f(0x1040), rl = p.f(0x1044);
            p.set_f(0x1058, int_to_f(nf) / fl);
            p.set_f(0x105C, int_to_f(nr) / rl);
        }
        // The pitch's tire terms: the static byte +0x07 x 0.01 over (the pitch inertia + mass x arm squared).
        const F t = int_to_f(program.b(Program::kStatic + 7)) * kCenti;
        const F a = p.f(0x10), b = p.f(0x14);
        p.set_f(0x12C, t / (pitch + (mass * a) * a));
        p.set_f(0x130, t / (pitch + (mass * b) * b));
    }
    base_grips(program, p);
    travel(s, program, p, 0);
    travel(s, program, p, 1);
    // The droops while the hold counter runs: the hub that hangs higher over its droop moved down to the other's.
    {
        const F df = p.f(0x314) - p.f(0x310), dr = p.f(0x36C) - p.f(0x368);
        p.set_f(0x34C, p.f(0x310)), p.set_f(0x3A4, p.f(0x368));
        if (dr < df) p.set_f(0x3A4, p.f(0x368) - (df - dr));
        else if (df < dr) p.set_f(0x34C, p.f(0x310) - (dr - df));
    }
    top_and_hold(s, program, p, race, in);
}

namespace {

// The engine's mirror's members that Gran Turismo 4's block keeps in another shape, from Gran Turismo 4's part of the record
// (loaded first): the 24-point curve thinned to 16 points (the points whose loss changes the curve least go first), at most
// 7 forward gears, the tire records with the 24 surfaces folded to the core's 16 rows (row 11 the wear grip's floor), the
// steering and camber curves, the inverses of the masses.
template <size_t N, size_t M> void table_into(const gt3r::sim::exact::Table<N>& from, gt3r::sim::exact::Table<M>& to) {
    to = {};
    to.n = s16(from.n < s16(M) ? from.n : s16(M));
    for (size_t k = 0; k < M && k < N; ++k) to.x[k] = from.x[k], to.y[k] = from.y[k];
}

void mirror(const ParamBlock& p, Record& r) {
    gt3r::sim::exact::Params& m = r.car.p;
    const Extra& x = r.x;
    // The engine's curve: 16 points at most.
    {
        struct Point {
            F x, y;
        };
        std::vector<Point> pts;
        for (s32 k = 0; k < x.drive.e.torque.n && k < 24; ++k) pts.push_back({x.drive.e.torque.x[size_t(k)], x.drive.e.torque.y[size_t(k)]});
        while (pts.size() > 16) {
            size_t best = 1;
            double least = 1e300;
            for (size_t i = 1; i + 1 < pts.size(); ++i) {
                const double x0 = pts[i - 1].x.host(), x1 = pts[i + 1].x.host(), y0 = pts[i - 1].y.host(), y1 = pts[i + 1].y.host();
                const double span = x1 - x0;
                const double on = span > 0 ? y0 + (y1 - y0) * (pts[i].x.host() - x0) / span : y0;
                const double loss = std::abs(on - pts[i].y.host()) * span;
                if (loss < least) least = loss, best = i;
            }
            pts.erase(pts.begin() + std::ptrdiff_t(best));
        }
        m.e.torque = {};
        m.e.torque.n = s16(pts.size());
        for (size_t k = 0; k < pts.size(); ++k) m.e.torque.x[k] = pts[k].x, m.e.torque.y[k] = pts[k].y;
    }
    if (m.d.gears > 7) m.d.gears = 7;
    // The tire records and the tire index by the core's rows.
    for (size_t t = 0; t < 6; ++t) {
        const rules::Tire& g = x.tires.tire[t];
        gt3r::sim::exact::TireRecord& e = m.tire[t];
        e.grip = g.grip, e.wear_rate = g.wear_rate;
        for (int row = 0; row < 16; ++row) e.mu[size_t(row)] = g.mu[size_t(surface_of_core_row(row))];
        e.mu[11] = g.wear_floor;
        table_into(g.curve_a, e.curve_a), table_into(g.curve_b, e.curve_b), table_into(g.curve_c, e.curve_c);
        e.peak_slip = g.peak_slip, e.slip_drive = g.slip_drive, e.slip_brake = g.slip_brake, e.brake_peak = g.brake_peak;
        e.curve_d = g.curve_d;
        table_into(g.load_curve, e.load_curve);
    }
    for (size_t a = 0; a < 2; ++a)
        for (int row = 0; row < 16; ++row) m.tire_index[a][size_t(row)] = x.tires.tire_index[a][size_t(surface_of_core_row(row))];
    for (size_t a = 0; a < 2; ++a) table_into(x.tires.camber_grip[a], m.camber_grip[a]);
    // The steering curves: the pad's (P +0x58, the spec's first), the wheel's (P +0xD0, its second) and the dirt's (P +0x94,
    // its third).
    auto steer = [&](u32 at, gt3r::sim::exact::Table<6>& t) {
        t = {};
        t.n = s16(p.sh(at) < 6 ? p.sh(at) : 6);
        for (u32 k = 0; k < 6; ++k) t.x[k] = p.f(at + 0x0C + 4 * k), t.y[k] = p.f(at + 0x24 + 4 * k);
    };
    steer(0x58, m.steer_b), steer(0xD0, m.steer_a), steer(0x94, m.steer_c);
    m.inv_mass = kOne / m.mass;
    m.inv_sprung_mass = p.f(0x4C) == kZero ? kZero : kOne / p.f(0x4C);
}

} // namespace

void load_params(const ParamBlock& p, const Program& program, Record& r) {
    using layout::Block;
    gt3r::sim::exact::fields(r.car, [&](const char*, int, u32 off, auto& m) {
        const layout::Place place = layout::place(off);
        if (place.block != Block::Params) return;
        using T = std::remove_cvref_t<decltype(m)>;
        std::memcpy(&m, &p.bytes[place.offset], std::is_same_v<T, gt3r::ee::F> ? 4 : sizeof(T));
    });
    extra_fields(r.x, [&](const char*, int, Block b, u32 off, auto& m) {
        if (b != Block::Params) return;
        using T = std::remove_cvref_t<decltype(m)>;
        std::memcpy(&m, &p.bytes[off], std::is_same_v<T, gt3r::ee::F> ? 4 : sizeof(T));
    });
    r.layout = rules::wheel_layout(program, p.b(0x30));
    mirror(p, r);
}

void build_record(const Spec& s, const Program& program, const rules::Tables& race, const ConvertIn& in, Record& r, ParamBlock* block,
                  ConvertGlobals* globals) {
    ParamBlock own;
    ParamBlock& p = block ? *block : own;
    p = ParamBlock{};
    p.base = 0x01000000;
    convert(s, program, race, in, p, globals);
    r.car.p = {};
    load_params(p, program, r);
    // What the car's set-up gives the record besides the block (0x354AC0 at its first step: the masses with the fuel, 0 here).
    r.x.chassis.mass = p.f(0x48);
    r.x.body.sprung_mass = p.f(0x4C);
    if (r.x.drive.d.kind == 3) rules::hybrid_start(r.x.drive.hybrid); // (0x3683C8)
}

} // namespace gt4r::sim
