#include "gt4r/race/picture_settings.h"

#include "base/ee_float.h"

#include <algorithm>
#include <cstring>
#include <mutex>

namespace gt4r::race {

namespace {

using gt3r::ee::F;
using gt3r::ee::int_to_f;
using gt3r::ee::madd;
using gt3r::ee::msub;
using gt3r::ee::to_int;

constexpr F kOne = F::raw(0x3F800000), kZero = F::raw(0), kLn2 = F::raw(0x3F317217), kInvLn2 = F::raw(0x3FB8AA3B);

F of(float v) { return F(v); }
float host(F v) {
    float f;
    std::memcpy(&f, &v.bits, 4);
    return f;
}

// The mantissa of x as a number 1 .. 2 and its exponent.
F mantissa(F x) { return F::raw((x.bits & 0x7FFFFFu) | 0x3F800000u); }
s32 exponent(F x) { return (s32(x.bits) >> 23) - 127; }

// The levels' log (0x4A8AD0): e ln 2 + N(m) / D(m) for x = m 2^e, N and D fifth powers of the mantissa summed on the FPU's
// accumulator (D: 30, 750, 3000, 3000, 750, 30; N: -137, -1625, -2000, 2000, 1625, 137).
F levels_log(F x) {
    const F m = mantissa(x), e = int_to_f(exponent(x));
    F acc = kZero + F::raw(0x41F00000); // 30
    const F m2 = m * m;
    acc = madd(acc, m, F::raw(0x443B8000)); // 750
    const F m3 = m * m2, m4 = m2 * m2;
    acc = madd(acc, m2, F::raw(0x453B8000)); // 3000
    const F m5 = m2 * m3;
    acc = madd(acc, m3, F::raw(0x453B8000));
    acc = madd(acc, m4, F::raw(0x443B8000));
    const F d = madd(acc, m5, F::raw(0x41F00000));
    acc = kZero - F::raw(0x43090000);           // 137
    acc = msub(acc, m, F::raw(0x44CB2000));     // 1625
    acc = msub(acc, m2, F::raw(0x44FA0000));    // 2000
    const F r = kOne / d;
    acc = madd(acc, m3, F::raw(0x44FA0000));
    acc = madd(acc, m4, F::raw(0x44CB2000));
    const F n = madd(acc, m5, F::raw(0x43090000));
    const F q = n * r;
    return e * kLn2 + q;
}

// 2^k for a whole k (0 under the smallest scale; the largest scale is not reached from the levels' and the gains' arguments).
F scale(s32 k) {
    const s32 v = k + 127;
    if (v < 0) return kZero;
    return F::raw(u32(std::min(v, 255)) << 23);
}

// The levels' exp (0x4A8BB0): x less its whole multiples of ln 2 (k = trunc(x / ln 2)), a series to the eighth power summed on the
// accumulator, times 2^k.
F levels_exp(F x) {
    const s32 k = to_int(x * kInvLn2);
    const F r = x - int_to_f(k) * kLn2;
    const F r2 = r * r;
    F acc = r * kOne;
    const F r3 = r * r2, r4 = r2 * r2;
    acc = madd(acc, r2, F::raw(0x3F000000)); // 1/2
    const F r5 = r2 * r3, r6 = r3 * r3, r7 = r3 * r4, r8 = r4 * r4;
    acc = madd(acc, r3, F::raw(0x3E2AAAAA)); // 1/6
    acc = madd(acc, r4, F::raw(0x3D2AAAAA)); // 1/24
    acc = madd(acc, r5, F::raw(0x3C088888)); // 1/120
    acc = madd(acc, r6, F::raw(0x3AB60B60)); // 1/720
    acc = madd(acc, r7, F::raw(0x39500D00)); // 1/5040
    acc = madd(acc, r8, F::raw(0x37D00D00)); // 1/40320
    const F s = madd(acc, kOne, kOne);
    if (k + 127 < 0) return kZero;
    return s * scale(k);
}

// The gain's log (0x57D5A8, x > 0): the same rational function as levels_log, its terms added one by one.
F gain_log(F x) {
    const F m = mantissa(x);
    const F m2 = m * m, a750 = m * F::raw(0x443B8000), a1625 = m * F::raw(0x44CB2000), m3 = m * m2;
    const F b3000 = m2 * F::raw(0x453B8000);
    F d = a750 + F::raw(0x41F00000);
    F n = F::raw(0xC3090000) - a1625;
    const F b2000 = m2 * F::raw(0x44FA0000), m4 = m2 * m2, c3000 = m3 * F::raw(0x453B8000), c2000 = m3 * F::raw(0x44FA0000);
    d = d + b3000;
    n = n - b2000;
    const F m5 = m2 * m3, d750 = m4 * F::raw(0x443B8000);
    d = d + c3000;
    const F d1625 = m4 * F::raw(0x44CB2000);
    n = n + c2000;
    const F e30 = m5 * F::raw(0x41F00000);
    d = d + d750;
    const F e137 = m5 * F::raw(0x43090000);
    n = n + d1625;
    const F e = int_to_f(exponent(x));
    d = d + e30;
    n = n + e137;
    return e * kLn2 + n / d;
}

// The gain's exp (0x57D488): levels_exp's series, its terms added one by one.
F gain_exp(F x) {
    const s32 k = to_int(x * kInvLn2);
    F r = x - int_to_f(k) * kLn2;
    const F r2 = r * r;
    F s = r + kOne;
    const F h = r2 * F::raw(0x3F000000);
    const F r3 = r * r2, r4 = r2 * r2;
    s = s + h;
    const F t6 = r3 * F::raw(0x3E2AAAAA), t24 = r4 * F::raw(0x3D2AAAAA), r5 = r2 * r3, r6 = r3 * r3;
    s = s + t6;
    const F r7 = r3 * r4, t120 = r5 * F::raw(0x3C088888), t720 = r6 * F::raw(0x3AB60B60);
    s = s + t24;
    const F t5040 = r7 * F::raw(0x39500D00), r8 = r4 * r4;
    s = s + t120;
    const F t40320 = r8 * F::raw(0x37D00D00);
    s = s + t720;
    s = s + t5040;
    s = s + t40320;
    if (k + 127 < 0) return kZero;
    return s * scale(k);
}

} // namespace

PictureSettings::Correction screen_correction(float b, float k, float saturation, float kelvin) {
    // (0x3EFC10, the photo mode's corrections too: the black level 0, the white 1 - k / 10, a gain of 2^b (1 at 0), red and blue
    // moved by -/+ (kelvin - 5500) x 0.2 / 2000.)
    PictureSettings::Correction c;
    const F fb = of(b);
    const F gain = fb == kZero ? kOne : gain_exp(fb * gain_log(F::raw(0x40000000)));
    const F shift = (of(kelvin) - F::raw(0x45ABE000)) * F::raw(0x3E4CCCCC) / F::raw(0x44FA0000);
    c.gamma[0] = host(gain * (kOne - shift)), c.gamma[1] = host(gain), c.gamma[2] = host(gain * (shift + kOne));
    c.contrast = host(kOne - of(k) * F::raw(0x3DCCCCCC));
    c.saturation = saturation;
    return c;
}

PictureSettings::Correction photo_filter(PictureSettings::Correction c, int filter) {
    if (filter == 2) {
        c.saturation = 0;
        return c;
    }
    if (filter != 1 && filter != 4) return c;
    // (0x3EFCE8's record, then 0x3EFD80: r *= r', g *= g', b *= b', black += 0, white += white' - 1, saturation *= saturation')
    const F gains[2][3] = {{kOne, kOne, kOne}, {F::raw(0x3FC00000), F::raw(0x3F733333), F::raw(0x3F199999)}};
    const F* g = gains[filter == 4 ? 1 : 0];
    for (int i = 0; i < 3; ++i) c.gamma[i] = host(of(c.gamma[i]) * g[i]);
    c.contrast = host(of(c.contrast) + (kOne - kOne));
    c.saturation = host(of(c.saturation) * (filter == 4 ? F::raw(0x3E999999) : F::raw(0x3F199999)));
    return c;
}

PictureSettings::Correction PictureSettings::correction() const {
    // (0x3EFDF0: b = brightness / 10, k = contrast x 3 / 5, the saturation (saturation + 10) / 10, 5500 + balance x 2000 / 10 kelvin.)
    const F ten = F::raw(0x41200000);
    const F b = int_to_f(std::clamp(v[0], -5, 5)) / ten;
    const F k = int_to_f(std::clamp(v[1], -5, 5)) * F::raw(0x40400000) / F::raw(0x40A00000);
    const F s = (int_to_f(std::clamp(v[2], -5, 5)) + ten) / ten;
    const F t = int_to_f(std::clamp(v[3], -5, 5)) * F::raw(0x44FA0000) / ten + F::raw(0x45ABE000);
    return screen_correction(host(b), host(k), host(s), host(t));
}

gt3r::render::DrawOptions::PostGrade PictureSettings::grade() const {
    const Correction c = correction();
    return correction_grade(c.gamma, 0, c.contrast, c.saturation, 255);
}

std::array<std::array<u8, 3>, 256> correction_levels(const float gamma[3], float black, float white, float most) {
    // (The race asks for these every frame, a camera's moving colors for new ones every frame: the last few are kept.)
    struct Key {
        float v[6];
        bool operator==(const Key& o) const { return std::memcmp(v, o.v, sizeof v) == 0; }
    };
    struct Entry {
        Key key;
        std::array<std::array<u8, 3>, 256> levels;
    };
    static std::mutex lock;
    static std::array<Entry, 4> kept{};
    static size_t kept_count = 0, next = 0;
    const Key key{{gamma[0], gamma[1], gamma[2], black, white, most}};
    {
        std::lock_guard<std::mutex> g(lock);
        for (size_t i = 0; i < kept_count; ++i)
            if (kept[i].key == key) return kept[i].levels;
    }
    std::array<std::array<u8, 3>, 256> out{};
    const F fblack = of(black), fwhite = of(white);
    const F inv = fblack == fwhite ? kZero : kOne / (fwhite - fblack);
    F e[3];
    for (int c = 0; c < 3; ++c) e[c] = kOne / gt3r::ee::max(of(gamma[c]), kZero);
    const F fmost = gt3r::ee::min(gt3r::ee::max(of(most), kZero), F::raw(0x437F0000));
    for (int i = 0; i < 256; ++i) {
        const F x = int_to_f(i) * F::raw(0x3B808080); // (i x 1/255, the program's constant)
        if (!(fblack < x)) continue;                   // (0, 0, 0)
        const F ln_t = levels_log(gt3r::ee::min((x - fblack) * inv, kOne));
        for (int c = 0; c < 3; ++c) {
            const F v = gt3r::ee::min(gt3r::ee::max(levels_exp(e[c] * ln_t) * F::raw(0x437F0000), kZero), fmost);
            out[size_t(i)][size_t(c)] = u8(to_int(v));
        }
    }
    std::lock_guard<std::mutex> g(lock);
    kept[next] = {key, out};
    next = (next + 1) % kept.size();
    kept_count = std::min(kept_count + 1, kept.size());
    return out;
}

gt3r::render::DrawOptions::PostGrade correction_grade(const float gamma[3], float black, float white, float saturation, float most) {
    gt3r::render::DrawOptions::PostGrade g;
    // (0x3EFD08: nothing to do when every gamma, the white level and the saturation are 1 and the black level 0; `most` under
    // 255 still holds the levels down.)
    g.on = black != 0 || white != 1 || saturation != 1 || most < 255;
    for (int c = 0; c < 3; ++c) g.on |= gamma[c] != 1;
    g.saturation = std::clamp(saturation, -1.0f, 1.0f);
    g.black = black;
    g.white = white;
    // The grade's own tables (DrawOptions::PostGrade::own_table): GT4's levels, in place of the engine's curve.
    g.own_table = true;
    const std::array<std::array<u8, 3>, 256> levels = correction_levels(gamma, black, white, most);
    for (size_t i = 0; i < 256; ++i)
        for (size_t c = 0; c < 3; ++c) g.table[i][c] = levels[i][c];
    return g;
}

} // namespace gt4r::race
