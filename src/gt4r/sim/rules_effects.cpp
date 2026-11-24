// Gran Turismo 4's own routines of the race step: the sliding measures and the car's dirt (rules_effects.h).

#include "gt4r/sim/rules_effects.h"

#include "base/ee_math.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/rules_wear.h"

#include <array>

namespace gt4r::sim::rules {

namespace {
const F kZero = F(), kOne = F::raw(0x3F800000);

// 0x3FA980: a row's counts 4; with `first` the first point of each curve at 0; the speed knee at most 50.
void fix_row(u8* r, bool first) {
    r[3] = 4, r[1] = 4, r[2] = 4;
    if (first) r[8] = 0, r[0x10] = 0, r[0x18] = 0;
    if (r[0x21] >= 0x33) r[0x21] = 0x32;
}
// 0x3FA9C0: a pair of bytes kept in order (the first below the second).
void fix_pair(u8& a, u8& b) {
    if (a < b) return;
    if (a == 0xFF) a = b == 0 ? 0xFF : u8(b - 1);
    else b = u8(a + 1);
}
} // namespace

SlideTables slide_tables(const u8* bytes) {
    using gt3r::ee::int_to_f;
    std::array<u8, 0x210> d{};
    for (size_t k = 0; k < d.size(); ++k) d[k] = bytes[k];
    // 0x3FAA10
    for (u32 k = 0; k < 4; ++k) {
        u8* r = &d[0x28 * k];
        fix_row(r, true);
        fix_row(r + 0xA0, false);
        u8& c = r[0xC0];
        if (c < 0x32) c = 0x32;
        else if (c >= 0xC9) c = 0xC8;
        fix_row(r + 0x140, false);
    }
    fix_pair(d[0x1E9], d[0x1EA]), fix_pair(d[0x1F0], d[0x1F1]), fix_pair(d[0x1F7], d[0x1F8]), fix_pair(d[0x201], d[0x203]);
    if (d[0x1E2] == 0) d[0x1E2] = 1;

    SlideTables t;
    const F scale_y = F::raw(0x451F6000); // 2550
    for (u32 g = 0; g < 3; ++g)
        for (u32 k = 0; k < 4; ++k) {
            const u8* r = &d[0xA0 * g + 0x28 * k];
            SlideCurves& s = t.set[g][k];
            t.by_lat[g][k] = r[0], t.by_slide[g][k] = r[0x1F];
            // 0x366B28: curve a
            {
                const F sx = r[0] ? F::raw(0x3DE38E37) : F::raw(0x3B64C385); // 0.4 km/h or 0.2 degrees a unit
                const F sy = int_to_f(r[0x1C]) / scale_y;
                const u8 n = r[1];
                for (u32 j = 0; j < n && j < 8; ++j) s.a.x[j] = int_to_f(r[4 + j]) * sx, s.a.y[j] = int_to_f(r[8 + j]) * sy;
                s.a.n = n;
            }
            // 0x366BC8: curve b, its negative side's points in reverse from +0x0C / +0x10, then its positive side's
            {
                const F sx = r[0x1F] ? F::raw(0x3D638E37) : F::raw(0x3B808080); // 0.2 km/h or 1 / 255 a unit
                const F sy0 = int_to_f(r[0x1D]) / scale_y, sy1 = int_to_f(r[0x1E]) / scale_y;
                const u32 n0 = r[2], n1 = r[3];
                for (u32 j = 0; j < n0 && j < 8; ++j) {
                    s.b.x[j] = (-int_to_f(r[0xC + n0 - 1 - j])) * sx;
                    s.b.y[j] = int_to_f(r[0x10 + n0 - 1 - j]) * sy0;
                }
                for (u32 j = 0; j < n1 && n0 + j < 8; ++j) {
                    s.b.x[n0 + j] = int_to_f(r[0x14 + j]) * sx;
                    s.b.y[n0 + j] = int_to_f(r[0x18 + j]) * sy1;
                }
                s.b.n = s16(n0 + n1);
            }
            // 0x366CD8
            const F centi = F::raw(0x3C23D70A), ramp = F::raw(0x4247FFFF);
            s.base = int_to_f(r[0x20]) * centi;
            const F v = int_to_f(r[0x21]) * F::raw(0x3E8E38E3); // km/h -> m/s
            s.knee = v / ramp;
            const F rest = F::raw(0x41DE38E2) - v;
            s.gain = int_to_f(r[0x22]) * centi * ramp / rest;
        }
    const u8* q = &d[0x200];
    const F tenth = F::raw(0x3DCCCCCC), kmh = F::raw(0x4031C71B);
    t.loose_scale = int_to_f(q[0]) * tenth;
    t.loose_x0 = int_to_f(q[1]) * kmh;
    t.loose_y0 = int_to_f(q[2]) * tenth;
    t.loose_x1 = int_to_f(q[3]) * kmh;
    t.loose_y1 = int_to_f(q[4]) * tenth;
    return t;
}

SlideTables slide_tables(const Program& program) {
    std::array<u8, 0x210> b{};
    for (u32 k = 0; k < b.size(); ++k) b[k] = program.b(kSlideBytes + k);
    return slide_tables(b.data());
}

int slide_kind(u8 s) {
    if ((s >= 2 && s <= 5) || s == 13 || s == 14) return 1;
    if (s == 6 || (s >= 20 && s <= 22)) return 2;
    return 0;
}

void slide_effects(ex::Car& c, const WheelLayout& l, const SlideTables& t, u8 course_kind) {
    using gt3r::ee::sqrt;
    F dirt = kZero;
    const int n = l.count < 4 ? l.count : 4;
    for (int i = 0; i < n; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        if (w.load == kZero) {
            if (w.smoke_count != 0) --w.smoke_count;
        } else
            w.smoke_count = 6;
        if (w.smoke_count == 0) {
            w.smoke_emit = kZero, w.skid_pitch = kOne, w.slip_intensity = kZero;
            continue;
        }
        const u8 s = w.surface;
        int kind = slide_kind(s);
        F angle = c.t[size_t(i)].slip_angle;
        if (angle < kZero) angle = -angle;
        F lat = gt3r::ee::cos(w.steer) * w.hub_vy + gt3r::ee::sin(w.steer) * w.hub_vx;
        if (lat < kZero) lat = -lat;
        const F along = gt3r::ee::cos(w.steer) * w.hub_vx - gt3r::ee::sin(w.steer) * w.hub_vy;
        F slide = along - w.rim_speed;
        if (along < kZero) slide = -slide;
        const F ratio = w.slip_ratio;
        // sqrt(b² + a²) of a measure's curves, and the speed ramp past its knee.
        auto measure = [&](int m, int k, F& ramp) {
            const SlideCurves& e = t.set[size_t(m)][size_t(k)];
            const F a = ex::lookup(e.a, t.by_lat[size_t(m)][size_t(k)] ? lat : angle);
            const F b = ex::lookup(e.b, t.by_slide[size_t(m)][size_t(k)] ? slide : ratio);
            ramp = w.speed_ramp - e.knee;
            return sqrt(b * b + a * a);
        };
        if (s != 1 && s != 10 && s != 11 && s != 21) {
            const SlideCurves& e0 = t.set[0][size_t(kind)];
            F ramp;
            F m = measure(0, kind, ramp);
            if (kZero < ramp) m = m + ramp * e0.gain;
            if (!(m == kZero)) m = m + e0.base;
            const bool starts = w.slip_intensity == e0.base && !(m == e0.base);
            w.slip_intensity = m;
            const SlideCurves& e1 = t.set[1][size_t(kind)];
            F p = e1.base + measure(1, kind, ramp);
            if (kZero < ramp) p = p + ramp * e1.gain;
            if (starts && (i & 1)) p = p * F::raw(0x3F400000);
            w.skid_pitch = p;
        } else {
            F k = w.load / F::raw(0x45191FFF) - F::raw(0x3E800000); // 2450 N, 0.25
            if (k < kZero) k = kZero;
            else if (kOne < k) k = kOne;
            F rim = w.rim_speed;
            if (rim < kZero) rim = -rim;
            F e = (rim - F::raw(0x3FB1C71B)) * F::raw(0x3E1374BD); // 5 km/h
            if (e < kZero) e = kZero;
            else if (kOne < e) e = kOne;
            w.slip_intensity = e * k * t.loose_scale;
            if (rim <= t.loose_x0) w.skid_pitch = t.loose_y0;
            else if (t.loose_x1 <= rim) w.skid_pitch = t.loose_y1;
            else w.skid_pitch = t.loose_y0 + (rim - t.loose_x0) * (t.loose_y1 - t.loose_y0) / (t.loose_x1 - t.loose_x0);
        }
        if (s == 12) kind = 1;
        const SlideCurves& e2 = t.set[2][size_t(kind)];
        F ramp;
        F m = measure(2, kind, ramp);
        if (kZero < ramp) m = m + ramp * e2.gain;
        if (s == 8) m = kZero;
        if (!(m == kZero)) m = m + e2.base;
        if (kOne < m) m = kOne;
        w.smoke_emit = m;
        if (kind != 0) dirt = dirt + m;
    }
    if ((course_kind == 0xFF ? 0 : course_kind) == 0) dirt = dirt * F::raw(0x40800000);
    const F add = dirt * F::raw(0x3A03126E) * c.dt;
    if ((c.x408[1] & 0xFF) == 0) { // 0x3438C8
        c.dirt = c.dirt + add;
        if (F::raw(0x3F4CCCCC) < c.dirt) c.dirt = F::raw(0x3F4CCCCC);
    }
}

} // namespace gt4r::sim::rules
