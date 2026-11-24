#include "gt4r/sim/ai/bspec_skill.h"

#include <algorithm>

namespace gt4r::sim::ai {

using F = gt3r::ee::F;

BSpecRecord BSpecRecord::from_bytes(const u8* p) {
    BSpecRecord r;
    auto h = [&](size_t o) { return u16(p[o] | (p[o + 1] << 8)); };
    for (size_t k = 0; k < 8; ++k) r.a[k] = h(2 * k), r.b[k] = h(0x10 + 2 * k), r.c[k] = h(0x20 + 2 * k);
    for (size_t k = 0; k < 4; ++k) r.d[k] = h(0x30 + 2 * k), r.e[k] = h(0x38 + 2 * k);
    return r;
}

BSpecTables BSpecTables::load(const Program& program) {
    BSpecTables t;
    if (!program.known()) return t;
    for (u32 k = 0; k < 8; ++k) t.weight[k] = program.b(0x623E40 + k);
    t.on = program.b(0x623E48) != 0;
    for (u32 k = 0; k < 6; ++k) t.base[k] = gt3r::s8(program.b(0x623E4A + k));
    return t;
}

int bspec_class(F rating) {
    // 0x4683B8: (x − 0.35) / 0.55 within [0, 1]
    F t = (rating - F::raw(0x3EB33333)) / F::raw(0x3F0CCCCC);
    if (t < F()) t = F();
    else if (F::raw(0x3F800000) < t) t = F::raw(0x3F800000);
    const int k = gt3r::ee::to_int((t * F::raw(0x41000000)).bits);
    return k < 8 ? k : 7;
}

namespace {

// 0x468BC8: a / 100 + (b / 100) x (x / 1000), x unsigned.
F blend(u8 a, gt3r::s8 b, u32 x) {
    const F hundred = F::raw(0x42C80000);
    const F fa = gt3r::ee::int_to_f(s32(a)) / hundred, fb = gt3r::ee::int_to_f(s32(b)) / hundred;
    const F fx = gt3r::ee::int_to_f(s32(x)) / F::raw(0x447A0000);
    return fa + fb * fx;
}

} // namespace

std::optional<BSpecSkill> bspec_skill(const BSpecTables& t, const BSpecRecord& r, F rating, bool course_flag) {
    if (!t.on) return std::nullopt;
    const int k = bspec_class(rating);
    // 0x467BE8: the classes weighed, [max(0, k − 4), min(8, k + 5))
    const int lo = k >= 5 ? k - 4 : 0, hi = k < 3 ? k + 5 : 8;
    u32 sa = 0, sb = 0, sc = 0, wa = 0, wb = 0, wc = 0;
    for (int c = lo; c < hi; ++c) {
        const u32 w = t.weight[size_t(c)];
        sa += std::min<u32>(r.a[size_t(c)], 0x400) * w;
        sb += std::min<u32>(r.b[size_t(c)], 0x400) * w;
        sc += std::min<u32>(r.c[size_t(c)], 0x100) * w;
        wa += w << 10, wb += w << 10, wc += w << 8;
    }
    const int half = k / 2;
    u32 hd = 0xFF, he = 0xFF;
    if (course_flag) hd = r.d[size_t(half)] >> 8, he = r.e[size_t(half)] >> 8;
    auto cap = [](u32 v, u32 m) { return v > m ? m : v; };
    const u32 x_a = cap(wa ? sa * 700 / wa : 0, 700), x_d = cap(hd * 300 / 255, 300), x_e = cap(he * 300 / 255, 300);
    const u32 x_b = cap(wb ? sb * 700 / wb : 0, 700) + x_d, x_c = cap(wc ? sc * 700 / wc : 0, 700) + x_e;
    BSpecSkill s;
    s.corner = blend(u8(t.base[0]), t.base[1], x_a + x_d);
    s.brake = blend(u8(t.base[2]), t.base[3], x_b);
    s.alongside = blend(u8(t.base[4]), t.base[5], x_c);
    return s;
}

} // namespace gt4r::sim::ai
